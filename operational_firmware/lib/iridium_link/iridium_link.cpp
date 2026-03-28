#include "iridium_link.h"

#include <Arduino.h>
#include <IridiumSBD.h>
#include <new>
#include <string.h>

#include "pins.h"
#include "project_config.h"
#include "settings.h"
#include "errors.h"
#include "debug.h"
#include "readings.h"
#include "state.h"
#include "sd_log.h"
#include "readings.h"
#include "cut_logic.h"
#include "status_led.h"


static HardwareSerial& SAT = Serial1;
static IridiumSBD modem(SAT);

static bool s_remote_cut_latched = false;
static bool s_modem_ready = false;
static bool s_modem_powered = false;
static bool s_cancel_deadline_active = false;
static uint32_t s_cancel_deadline_ms = 0;

static uint32_t s_last_tx_ms = 0;
static uint32_t s_last_begin_attempt_ms = 0;
static uint8_t  s_fail_count = 0;

static volatile bool s_iridium_busy = false;
static bool s_isbd_console_line_open = false;
static bool s_isbd_diag_line_open = false;

bool iridiumIsBusy() { return s_iridium_busy; }

static void debugPrintSessionStatus1Hz() {
    if (!DEBUG_SERIAL) return;

    Serial.print("[IR-LOOP] t=");
    Serial.print(g_state.t_power_s);
    Serial.print("s ");

    Serial.print("mode=");
    Serial.print((g_state.system_mode == MODE_CONFIG) ? "CFG" : "NORM");
    Serial.print(" ");

    Serial.print("flight=");
    switch (g_state.flight_state) {
        case FLIGHT_GROUND:     Serial.print("GND"); break;
        case FLIGHT_IN_FLIGHT:  Serial.print("FLT"); break;
        case FLIGHT_TERMINATED: Serial.print("TERM"); break;
        default:                Serial.print("?"); break;
    }
    Serial.print(" ");

    Serial.print("launch=");
    Serial.print(g_state.launch_detected ? "Y" : "N");
    Serial.print(" ");

    Serial.print("cut=");
    Serial.print(g_state.cut_fired ? "Y" : "N");
    if (g_state.cut_fired) {
        Serial.print(" reason=");
        Serial.print((int)g_state.cut_reason);
    }

    Serial.print(" ext=[");
    for (uint8_t i = 0; i < NUM_EXTERNAL_INPUTS; i++) {
        Serial.print(g_readings.ext[i].debounced_active ? "1" : "0");
        if (i + 1 < NUM_EXTERNAL_INPUTS) Serial.print(",");
    }
    Serial.print("]");

    Serial.print(" env=");
    if (g_readings.temp_valid) {
        Serial.print("T=");
        Serial.print(g_readings.temp_c, 2);
        Serial.print("C");
    } else {
        Serial.print("T=NA");
    }
    Serial.print(" ");
    if (g_readings.pressure_valid) {
        Serial.print("P=");
        Serial.print(g_readings.pressure_hpa, 2);
        Serial.print("hPa");
    } else {
        Serial.print("P=NA");
    }
    Serial.print(" ");
    if (g_readings.humidity_valid) {
        Serial.print("RH=");
        Serial.print(g_readings.humidity_pct, 1);
        Serial.print("%");
    } else {
        Serial.print("RH=NA");
    }

    Serial.print(" gps=");
    if (g_readings.gps_lat_valid) {
        Serial.print(g_readings.gps_lat_deg, 6);
    } else {
        Serial.print("NA");
    }
    Serial.print(",");
    if (g_readings.gps_lon_valid) {
        Serial.print(g_readings.gps_lon_deg, 6);
    } else {
        Serial.print("NA");
    }
    Serial.print(",");
    if (g_readings.gps_alt_valid) {
        Serial.print(g_readings.gps_alt_m, 1);
    } else {
        Serial.print("NA");
    }
    Serial.print("m");

    Serial.println();
}

static void configureModemInstance() {
    modem.adjustATTimeout(IRIDIUM_BOOT_AT_TIMEOUT_S);
    modem.setPowerProfile(IridiumSBD::DEFAULT_POWER_PROFILE);
}

static void resetModemInstance() {
    new (&modem) IridiumSBD(SAT);
    configureModemInstance();
}

static void clearCancelDeadline() {
    s_cancel_deadline_active = false;
    s_cancel_deadline_ms = 0;
}

static void startCancelDeadline(uint32_t duration_ms) {
    s_cancel_deadline_active = (duration_ms > 0);
    s_cancel_deadline_ms = millis() + duration_ms;
}

static bool cancelDeadlineExpired() {
    if (!s_cancel_deadline_active) return false;
    return (int32_t)(millis() - s_cancel_deadline_ms) >= 0;
}

// Weak hook: application may override to run time-critical work during long Iridium sessions.
// Keep this FAST (no SD writes, no long I/O).
void iridiumServiceDuringSession() __attribute__((weak));
void iridiumServiceDuringSession() {
    const uint32_t now_ms = millis();

    // Keep LED activity alive while the modem session blocks the normal main loop.
    statusLedUpdateFast(now_ms);

    // Always keep UART drained (cheap, prevents overflow)
    readingsDrainGPS();

    if (!stateTick1Hz(now_ms)) return;

    stateOn1HzTick(now_ms);

    // Update all readings (I2C + GPS derived fields)
    readingsUpdate1Hz(now_ms);

    stateUpdateTerminationDetector1Hz(now_ms);

    // NEW: evaluate/actuate cut during Iridium sessions too
    cutLogicUpdate1Hz(now_ms);

    // Keep 1 Hz logging cadence (queues during Iridium busy)
    sdLogUpdate1Hz(now_ms);

    // Refresh the 1 Hz LED state after updating readings/cut logic/state.
    statusLedUpdate1Hz(now_ms);
    statusLedUpdateFast(now_ms);

    // Show that the safety loop is still alive while a modem session blocks
    // the normal top-level loop path.
    debugPrintSessionStatus1Hz();
}



// IridiumSBD calls this periodically during long operations (weak in library; override here).
bool ISBDCallback() {
    iridiumServiceDuringSession();
    return !cancelDeadlineExpired(); // true = continue, false = cancel
}

void ISBDConsoleCallback(IridiumSBD* device, char c) {
    (void)device;
    if (!DEBUG_SERIAL) return;

    if (!s_isbd_console_line_open) {
        Serial.print("[ISBD-CON] ");
        s_isbd_console_line_open = true;
    }

    if (c == '\r') return;
    if (c == '\n') {
        Serial.println();
        s_isbd_console_line_open = false;
        return;
    }

    Serial.print(c);
}

void ISBDDiagsCallback(IridiumSBD* device, char c) {
    (void)device;
    if (!DEBUG_SERIAL) return;

    if (!s_isbd_diag_line_open) {
        Serial.print("[ISBD-DIAG] ");
        s_isbd_diag_line_open = true;
    }

    if (c == '\r') return;
    if (c == '\n') {
        Serial.println();
        s_isbd_diag_line_open = false;
        return;
    }

    Serial.print(c);
}

static void satPowerOn() {
    if (s_modem_powered) return;
    // Modem ON is high-impedance on the control pin. The modem pulls it high internally.
    pinMode(PIN_SAT_POWER, INPUT);
    s_modem_powered = true;
}

static void satPowerOff() {
    if (!s_modem_powered) return;
    // Modem OFF is an asserted low on the control pin.
    pinMode(PIN_SAT_POWER, OUTPUT);
    digitalWrite(PIN_SAT_POWER, LOW);
    s_modem_powered = false;
    resetModemInstance();
}

static void markModemNotReady() {
    s_modem_ready = false;
}

static int readSatAvalRaw() {
    pinMode(PIN_SAT_AVAL, INPUT);
    return digitalRead(PIN_SAT_AVAL);
}

static void satUartDrain() {
    while (SAT.available() > 0) {
        (void)SAT.read();
    }
}

static bool satWaitForSubstring(const char* needle, uint32_t timeout_ms) {
    if (!needle || !needle[0]) return false;

    const size_t needle_len = strlen(needle);
    char window[48];
    memset(window, 0, sizeof(window));
    size_t used = 0;

    const uint32_t start_ms = millis();
    while ((uint32_t)(millis() - start_ms) < timeout_ms) {
        while (SAT.available() > 0) {
            const char c = (char)SAT.read();

            if (used + 1 < sizeof(window)) {
                window[used++] = c;
                window[used] = '\0';
            } else {
                memmove(window, window + 1, sizeof(window) - 2);
                window[sizeof(window) - 2] = c;
                window[sizeof(window) - 1] = '\0';
            }

            if (strstr(window, needle) != nullptr) {
                return true;
            }
        }

        delay(5);
    }

    return false;
}

static bool modemRespondsToAT() {
    satUartDrain();
    SAT.print("AT\r");
    return satWaitForSubstring("OK\r\n", IRIDIUM_PROBE_TIMEOUT_MS);
}

static void logRawSatResponse(const char* label, uint32_t timeout_ms) {
    char buf[128];
    size_t used = 0;
    memset(buf, 0, sizeof(buf));

    const uint32_t start_ms = millis();
    while ((uint32_t)(millis() - start_ms) < timeout_ms) {
        while (SAT.available() > 0) {
            const char c = (char)SAT.read();
            if (used + 1 < sizeof(buf)) {
                buf[used++] = c;
                buf[used] = '\0';
            }
        }
        delay(5);
    }

    debugPrint("[INFO] ");
    Serial.print(label);
    debugPrint(" raw=");
    if (used == 0) {
        Serial.println("<none>");
    } else {
        Serial.println(buf);
    }
}

static void rawProbeCommand(const char* label, const char* cmd, uint32_t timeout_ms) {
    satUartDrain();
    SAT.print(cmd);
    logRawSatResponse(label, timeout_ms);
}

static bool ensureModemReady(uint32_t begin_timeout_ms) {
    if (s_modem_ready) return true;

    satPowerOn();
    delay(250);
    s_last_begin_attempt_ms = millis();

    SAT.begin(IRIDIUM_SERIAL_BAUD, SERIAL_8N1, PIN_SAT_RX, PIN_SAT_TX);
    configureModemInstance();

    debugPrint("[INFO] Iridium AVAL=");
    Serial.println(readSatAvalRaw());

    if (!modemRespondsToAT()) {
        debugPrintln("[WARN] Iridium raw AT probe failed");
        markModemNotReady();
        satPowerOff();
        return false;
    }

    debugPrintln("[INFO] Iridium raw AT probe OK");
    rawProbeCommand("Iridium AT+CGMR", "AT+CGMR\r", IRIDIUM_PROBE_TIMEOUT_MS);
    rawProbeCommand("Iridium AT+CSQ", "AT+CSQ\r", IRIDIUM_PROBE_TIMEOUT_MS);

    debugPrintln("[INFO] Iridium modem begin attempt");
    startCancelDeadline(begin_timeout_ms);
    const int err = modem.begin();
    clearCancelDeadline();
    if (err != ISBD_SUCCESS && err != ISBD_ALREADY_AWAKE) {
        markModemNotReady();
        satPowerOff();
        debugPrint("[WARN] Iridium begin failed err=");
        Serial.println(err);
        return false;
    }

    s_modem_ready = true;
    if (err == ISBD_ALREADY_AWAKE) {
        debugPrintln("[INFO] Iridium modem already awake");
    } else {
        debugPrintln("[INFO] Iridium begin OK");
    }
    return true;
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

static const char* iridiumErrName(int err) {
    switch (err) {
        case ISBD_SUCCESS: return "SUCCESS";
        case ISBD_ALREADY_AWAKE: return "ALREADY_AWAKE";
        case ISBD_SERIAL_FAILURE: return "SERIAL_FAILURE";
        case ISBD_PROTOCOL_ERROR: return "PROTOCOL_ERROR";
        case ISBD_CANCELLED: return "CANCELLED";
        case ISBD_NO_MODEM_DETECTED: return "NO_MODEM_DETECTED";
        case ISBD_SBDIX_FATAL_ERROR: return "SBDIX_FATAL_ERROR";
        case ISBD_SENDRECEIVE_TIMEOUT: return "SENDRECEIVE_TIMEOUT";
        case ISBD_RX_OVERFLOW: return "RX_OVERFLOW";
        case ISBD_REENTRANT: return "REENTRANT";
        case ISBD_IS_ASLEEP: return "IS_ASLEEP";
        case ISBD_NO_SLEEP_PIN: return "NO_SLEEP_PIN";
        case ISBD_NO_NETWORK: return "NO_NETWORK";
        case ISBD_MSG_TOO_LONG: return "MSG_TOO_LONG";
        default: return "UNKNOWN";
    }
}

static bool doTelemetrySendAndReceive() {
    if (!ensureModemReady(IRIDIUM_BEGIN_TIMEOUT_MS)) {
        return false;
    }

    // SBDIX can legitimately take much longer than boot-time AT probes.
    modem.adjustATTimeout(IRIDIUM_SESSION_AT_TIMEOUT_S);

    int csq = -1;
    const int csq_err = modem.getSignalQuality(csq);
    debugPrint("[INFO] Iridium CSQ err=");
    Serial.print(csq_err);
    debugPrint(" name=");
    Serial.print(iridiumErrName(csq_err));
    debugPrint(" value=");
    Serial.println(csq);

    // If user disables TX in this phase by setting interval 0, caller won’t call us.
    // Build a compact CSV payload kept under the RockBLOCK credit step threshold:
    // T,<t_power_s>,<flight>,<lat4>,<lon4>,<alt_m>,<temp_c>,<p_hpa>,<cut_reason>
    char msg[160];
    msg[0] = '\0';

    const bool lat_valid = g_readings.gps_lat_valid;
    const bool lon_valid = g_readings.gps_lon_valid;
    const bool alt_valid = g_readings.gps_alt_valid;
    const bool temp_valid = g_readings.temp_valid;
    const bool pres_valid = g_readings.pressure_valid;

    const long alt_m = alt_valid ? lroundf(g_readings.gps_alt_m) : 0L;
    const long temp_c = temp_valid ? lroundf(g_readings.temp_c) : 0L;
    const long pres_hpa = pres_valid ? lroundf(g_readings.pressure_hpa) : 0L;

    char lat_buf[20];
    char lon_buf[20];
    if (lat_valid) snprintf(lat_buf, sizeof(lat_buf), "%.4f", (double)g_readings.gps_lat_deg);
    else           snprintf(lat_buf, sizeof(lat_buf), "NA");
    if (lon_valid) snprintf(lon_buf, sizeof(lon_buf), "%.4f", (double)g_readings.gps_lon_deg);
    else           snprintf(lon_buf, sizeof(lon_buf), "NA");

    snprintf(msg, sizeof(msg), "T,%lu,%u,%s,%s,%ld,%ld,%ld,%u",
             (unsigned long)g_state.t_power_s,
             (unsigned)g_state.flight_state,
             lat_buf,
             lon_buf,
             alt_m,
             temp_c,
             pres_hpa,
             (unsigned)g_state.cut_reason);

    // Use send+receive every time to avoid extra mailbox sessions.
    // Rx buffer sized to max SBD MT payload (270 bytes).
    uint8_t rx[270];
    size_t rxLen = sizeof(rx);

    // Prefer nullptr for zero-length MO; if your toolchain complains, use the dummy variant below.
    const uint8_t* tx = (const uint8_t*)msg;
    size_t txLen = strnlen(msg, sizeof(msg));

    debugPrint("[INFO] Iridium TX len=");
    Serial.print((unsigned)txLen);
    debugPrint(" payload=");
    Serial.println(msg);

    s_iridium_busy = true;
    int err = modem.sendReceiveSBDBinary(tx, txLen, rx, rxLen);
    s_iridium_busy = false;
    sdLogFlushQueued();



    // Dummy-pointer fallback if overload resolution doesn't like nullptr:
    // uint8_t dummy = 0;
    // int err = modem.sendReceiveSBDBinary((uint8_t*)tx, txLen, rx, rxLen);

    if (err != ISBD_SUCCESS) {
        debugPrint("[WARN] Iridium sendReceive err=");
        Serial.print(err);
        debugPrint(" name=");
        Serial.println(iridiumErrName(err));
        modem.adjustATTimeout(IRIDIUM_BOOT_AT_TIMEOUT_S);
        if (err == ISBD_IS_ASLEEP ||
            err == ISBD_SERIAL_FAILURE ||
            err == ISBD_NO_MODEM_DETECTED ||
            err == ISBD_PROTOCOL_ERROR) {
            markModemNotReady();
        }
        return false;
    }

    modem.adjustATTimeout(IRIDIUM_BOOT_AT_TIMEOUT_S);

    if (rxLen > 0) {
        handleRxMessage(rx, rxLen);
    }

    return true;
}

void iridiumInit() {
    s_remote_cut_latched = false;
    s_modem_ready = false;
    s_modem_powered = false;
    s_last_tx_ms = 0;
    s_last_begin_attempt_ms = 0;
    s_fail_count = 0;
    clearCancelDeadline();
    resetModemInstance();

    // Boot must stay fast and recoverable. Actual modem wake/begin is handled
    // by iridiumUpdate1Hz based on the next scheduled transmit time.
    if (!g_settings.iridium.enabled) {
        satPowerOff();
        errorClear(ERR_IRIDIUM);
        return;
    }

    const uint32_t tx_interval_s = currentTxIntervalS();
    if (tx_interval_s > 0 && tx_interval_s <= IRIDIUM_BOOT_PREPOWER_WINDOW_S) {
        debugPrintln("[INFO] Iridium pre-powering modem after setup");
        satPowerOn();
    } else {
        satPowerOff();
    }
    errorClear(ERR_IRIDIUM);
}

bool iridiumGetRemoteCutRequestAndClear() {
    const bool v = s_remote_cut_latched;
    s_remote_cut_latched = false;
    return v;
}

void iridiumUpdate1Hz(uint32_t now_ms) {
    // Disabled: nothing to do.
    if (!g_settings.iridium.enabled) {
        satPowerOff();
        markModemNotReady();
        errorClear(ERR_IRIDIUM);
        return;
    }

    // Guard: never start an Iridium session while in config mode
    // (keeps AP/web UI responsive; avoids long blocking calls on the ground).
    if (g_state.system_mode == MODE_CONFIG) {
        satPowerOff();
        markModemNotReady();
        return;
    }

    const uint32_t tx_interval_s = currentTxIntervalS();
    if (tx_interval_s == 0) {
        satPowerOff();
        markModemNotReady();
        errorClear(ERR_IRIDIUM);
        return;
    }

    const uint32_t tx_interval_ms = tx_interval_s * 1000UL;
    const uint32_t next_due_ms = (s_last_tx_ms == 0) ? now_ms : (s_last_tx_ms + tx_interval_ms);
    const bool due_now = (s_last_tx_ms == 0) || ((int32_t)(now_ms - next_due_ms) >= 0);
    const uint32_t time_until_due_ms = due_now ? 0 : (next_due_ms - now_ms);
    const uint32_t keep_awake_ms = IRIDIUM_KEEP_AWAKE_WINDOW_S * 1000UL;
    const uint32_t prewake_ms = IRIDIUM_PREWAKE_WINDOW_S * 1000UL;

    if (time_until_due_ms > keep_awake_ms) {
        satPowerOff();
        markModemNotReady();
        return;
    }

    const bool in_prewake_window = (time_until_due_ms <= prewake_ms);
    if (in_prewake_window) {
        satPowerOn();
    }

    if (in_prewake_window && !s_modem_ready) {
        const bool retry_due = (s_last_begin_attempt_ms == 0) ||
                               ((uint32_t)(now_ms - s_last_begin_attempt_ms) >= (IRIDIUM_BEGIN_RETRY_INTERVAL_S * 1000UL));
        if (retry_due) {
            if (ensureModemReady(IRIDIUM_BEGIN_TIMEOUT_MS)) {
                s_fail_count = 0;
                errorClear(ERR_IRIDIUM);
            } else {
                if (s_fail_count < 255) s_fail_count++;
                if (s_fail_count >= IRIDIUM_FAILS_BEFORE_ERROR) errorSet(ERR_IRIDIUM);
            }
        }
    }

    if (due_now) {
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
