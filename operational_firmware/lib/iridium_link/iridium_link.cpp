#include "iridium_link.h"

#include <Arduino.h>
#include <IridiumSBD.h>
#include <string.h>

#include "pins.h"
#include "project_config.h"
#include "settings.h"
#include "errors.h"
#include "debug.h"
#include "readings.h"
#include "state.h"
#include "sd_log.h"


static HardwareSerial& SAT = Serial1;
static IridiumSBD modem(SAT);

static bool s_remote_cut_latched = false;

static uint32_t s_last_tx_ms = 0;
static uint8_t  s_fail_count = 0;

static volatile bool s_iridium_busy = false;

bool iridiumIsBusy() { return s_iridium_busy; }

// Weak hook: application may override to run time-critical work during long Iridium sessions.
// Keep this FAST (no SD writes, no long I/O).
void iridiumServiceDuringSession() __attribute__((weak));
void iridiumServiceDuringSession() {
    readingsDrainGPS();
    // Default: keep 1 Hz timebase/state/termination detector alive.
    const uint32_t now_ms = millis();
    if (stateTick1Hz(now_ms)) {
        stateOn1HzTick(now_ms);
        stateUpdateTerminationDetector1Hz(now_ms);
        sdLogUpdate1Hz(now_ms);
    }
}

// IridiumSBD calls this periodically during long operations (weak in library; override here).
bool ISBDCallback() {
    iridiumServiceDuringSession();
    return true; // true = continue, false = cancel
}

static void satPowerOn() {
    pinMode(PIN_SAT_POWER, OUTPUT);
    if (SAT_POWER_ACTIVE_HIGH) digitalWrite(PIN_SAT_POWER, HIGH);
    else                       digitalWrite(PIN_SAT_POWER, LOW);
}

static void satPowerOff() {
    pinMode(PIN_SAT_POWER, OUTPUT);
    if (SAT_POWER_ACTIVE_HIGH) digitalWrite(PIN_SAT_POWER, LOW);
    else                       digitalWrite(PIN_SAT_POWER, HIGH);
}

static uint32_t currentTxIntervalS() {
    // Phase selection:
    // - Ground: not launched
    // - Ascent: launched, not terminated
    // - Descent: terminated, within descent_duration_s
    // - Beacon: terminated, beyond descent_duration_s
    if (!g_state.launch_detected) {
        return g_settings.iridium.ground_interval_s;
    }

    if (!g_state.terminated) {
        return g_settings.iridium.ascent_interval_s;
    }

    // Terminated: decide descent vs beacon
    const uint32_t dt = g_state.t_terminated_s; // tick domain seconds since termination
    const uint32_t descent_window = g_settings.iridium.descent_duration_s;

    if (descent_window == 0) {
        // If user sets 0, treat as "go straight to beacon"
        return g_settings.iridium.beacon_interval_s;
    }

    if (dt <= descent_window) {
        return g_settings.iridium.descent_interval_s;
    }

    return g_settings.iridium.beacon_interval_s;
}

static bool parseCutCommand(const char* msg) {
    // Expected format: "CUT,<serial>,<token>"
    // Example: "CUT,1234567,CUTDOWN"
    if (!msg) return false;

    // Must start with "CUT,"
    if (!(msg[0] == 'C' || msg[0] == 'c')) return false;
    if (!(msg[1] == 'U' || msg[1] == 'u')) return false;
    if (!(msg[2] == 'T' || msg[2] == 't')) return false;
    if (msg[3] != ',') return false;

    // Parse serial number
    const char* p = msg + 4;
    uint32_t serial = 0;
    bool any = false;

    while (*p >= '0' && *p <= '9') {
        any = true;
        serial = serial * 10u + (uint32_t)(*p - '0');
        p++;
        if (serial > 9999999u) return false;
    }
    if (!any) return false;
    if (*p != ',') return false;
    p++;

    // Remaining is token (up to end, allow trailing whitespace)
    char token_rx[32];
    size_t n = 0;
    while (*p && n < sizeof(token_rx) - 1) {
        char c = *p++;
        if (c == '\r' || c == '\n') break;
        token_rx[n++] = c;
    }
    token_rx[n] = '\0';

    // Basic trim right
    while (n > 0 && (token_rx[n - 1] == ' ' || token_rx[n - 1] == '\t')) {
        token_rx[n - 1] = '\0';
        n--;
    }

    if (serial != g_settings.device.serial_number) return false;
    if (!g_settings.iridium.cutdown_on_command) return false;

    // Compare token
    if (strncmp(token_rx, g_settings.iridium.cutdown_token,
                sizeof(g_settings.iridium.cutdown_token)) != 0) {
        return false;
    }

    return true;
}

static void appendFloat(char* dst, size_t dstlen, const char* fmt, float v) {
    // Helper: write a float only if finite; else write "NA"
    // fmt should include leading comma, e.g. ",%.5f"
    if (!dst || dstlen == 0) return;
    const size_t used = strlen(dst);
    if (used >= dstlen - 1) return;

    if (isfinite(v)) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), fmt, (double)v);
        strncat(dst, tmp, dstlen - used - 1);
    } else {
        strncat(dst, ",NA", dstlen - used - 1);
    }
}

static void handleRxMessage(const uint8_t* rx, size_t rxLen) {
    if (!rx || rxLen == 0) return;

    // Treat as ASCII command for v1
    char msg[271];
    size_t n = (rxLen > 270) ? 270 : rxLen;
    memcpy(msg, rx, n);
    msg[n] = '\0';

    // Cost control + safety: ignore remote cut once cut or terminated
    if (g_state.cut_fired || g_state.terminated) {
        debugPrintln("[INFO] Iridium MT received after cut/termination (ignored)");
        return;
    }

    if (parseCutCommand(msg)) {
        s_remote_cut_latched = true;
        debugPrintln("[INFO] Iridium remote cut command accepted");
    } else {
        debugPrintln("[INFO] Iridium message received (ignored)");
    }
}

static bool doTelemetrySendAndReceive() {
    // If user disables TX in this phase by setting interval 0, caller won’t call us.
    // Build a compact CSV-ish payload:
    // T,<serial>,<t_power_s>,<flight>,<lat>,<lon>,<alt>,<temp>,<p>,<rh>,<cut>,<reason>
    char msg[160];
    msg[0] = '\0';

    const uint32_t serial = g_settings.device.serial_number;

    snprintf(msg, sizeof(msg), "T,%lu,%lu,%u",
             (unsigned long)serial,
             (unsigned long)g_state.t_power_s,
             (unsigned)g_state.flight_state);

    // GPS (use NAN when invalid so helper outputs NA)
    const float lat = g_readings.gps_lat_valid ? g_readings.gps_lat_deg : NAN;
    const float lon = g_readings.gps_lon_valid ? g_readings.gps_lon_deg : NAN;
    const float alt = g_readings.gps_alt_valid ? g_readings.gps_alt_m : NAN;

    const float temp = g_readings.temp_valid ? g_readings.temp_c : NAN;
    const float pres = g_readings.pressure_valid ? g_readings.pressure_hpa : NAN;
    const float rh   = g_readings.humidity_valid ? g_readings.humidity_pct : NAN;

    appendFloat(msg, sizeof(msg), ",%.6f", lat);
    appendFloat(msg, sizeof(msg), ",%.6f", lon);
    appendFloat(msg, sizeof(msg), ",%.1f",  alt);
    appendFloat(msg, sizeof(msg), ",%.2f",  temp);
    appendFloat(msg, sizeof(msg), ",%.2f",  pres);
    appendFloat(msg, sizeof(msg), ",%.2f",  rh);

    // cut + reason
    {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), ",%u,%u",
                 (unsigned)(g_state.cut_fired ? 1 : 0),
                 (unsigned)g_state.cut_reason);
        strncat(msg, tmp, sizeof(msg) - strlen(msg) - 1);
    }

    // Use send+receive every time to avoid extra mailbox sessions.
    // Rx buffer sized to max SBD MT payload (270 bytes).
    uint8_t rx[270];
    size_t rxLen = sizeof(rx);

    // Prefer nullptr for zero-length MO; if your toolchain complains, use the dummy variant below.
    const uint8_t* tx = (const uint8_t*)msg;
    size_t txLen = strnlen(msg, sizeof(msg));

    s_iridium_busy = true;
    int err = modem.sendReceiveSBDBinary(tx, txLen, rx, rxLen);
    s_iridium_busy = false;
    sdLogFlushQueued();



    // Dummy-pointer fallback if overload resolution doesn't like nullptr:
    // uint8_t dummy = 0;
    // int err = modem.sendReceiveSBDBinary((uint8_t*)tx, txLen, rx, rxLen);

    if (err != ISBD_SUCCESS) {
        return false;
    }

    if (rxLen > 0) {
        handleRxMessage(rx, rxLen);
    }

    return true;
}

void iridiumInit() {
    s_remote_cut_latched = false;
    s_last_tx_ms = 0;
    s_fail_count = 0;

    // If disabled, keep modem off and clear error
    if (!g_settings.iridium.enabled) {
        satPowerOff();
        errorClear(ERR_IRIDIUM);
        return;
    }

    satPowerOn();
    delay(250);

    SAT.begin(IRIDIUM_SERIAL_BAUD, SERIAL_8N1, PIN_SAT_RX, PIN_SAT_TX);

    modem.setPowerProfile(IridiumSBD::DEFAULT_POWER_PROFILE);

    const int err = modem.begin();
    if (err != ISBD_SUCCESS) {
        // Don’t immediately hard-fail; we’ll retry on next scheduled operation.
        s_fail_count = 1;
        if (s_fail_count >= IRIDIUM_FAILS_BEFORE_ERROR) errorSet(ERR_IRIDIUM);
        debugPrintln("[WARN] Iridium begin failed");
    } else {
        s_fail_count = 0;
        errorClear(ERR_IRIDIUM);
        debugPrintln("[INFO] Iridium begin OK");
    }
}

bool iridiumGetRemoteCutRequestAndClear() {
    const bool v = s_remote_cut_latched;
    s_remote_cut_latched = false;
    return v;
}

void iridiumUpdate1Hz(uint32_t now_ms) {
    // Disabled: nothing to do.
    if (!g_settings.iridium.enabled) {
        errorClear(ERR_IRIDIUM);
        return;
    }

    // Guard: never start an Iridium session while in config mode
    // (keeps AP/web UI responsive; avoids long blocking calls on the ground).
    if (g_state.system_mode == MODE_CONFIG) {
        return;
    }

    // ---- Telemetry TX scheduling ----
    const uint32_t tx_interval_s = currentTxIntervalS();
    if (tx_interval_s > 0) {
        const uint32_t tx_interval_ms = tx_interval_s * 1000UL;
        if (s_last_tx_ms == 0 || (uint32_t)(now_ms - s_last_tx_ms) >= tx_interval_ms) {
            s_last_tx_ms = now_ms;

            const bool ok = doTelemetrySendAndReceive();
            if (ok) {
                s_fail_count = 0;
                errorClear(ERR_IRIDIUM);
            } else {
                if (s_fail_count < 255) s_fail_count++;
                if (s_fail_count >= IRIDIUM_FAILS_BEFORE_ERROR) errorSet(ERR_IRIDIUM);
                debugPrintln("[WARN] Iridium telemetry send/receive failed");
            }
        }
    }
}
