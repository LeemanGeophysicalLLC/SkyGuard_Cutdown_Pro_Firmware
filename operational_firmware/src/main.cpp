// main.cpp
/**
 * @file main.cpp
 * @brief Top-level firmware control loop for SkyGuard Cutdown Pro.
 */

#include <Arduino.h>
#include "project_config.h"
#include "pins.h"
#include "debug.h"
#include "settings.h"
#include "state.h"
#include "webconfig.h"
#include "servo_release.h"
#include "readings.h"
#include "cut_logic.h"
#include "status_led.h"
#include "sd_log.h"
#include "iridium_link.h"
#include "errors.h"

// -------------------------
// Debug helpers
// -------------------------

/**
 * @brief Print a compact one-line status snapshot (1 Hz).
 */
/**
 * @brief Print a compact one-line status snapshot (1 Hz).
 */
static void debugPrintStatus1Hz() {
    if (!DEBUG_SERIAL) return;

    Serial.print("t=");
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

    // External inputs (debounced view from readings)
    Serial.print(" ext=[");
    for (uint8_t i = 0; i < NUM_EXTERNAL_INPUTS; i++) {
        Serial.print(g_readings.ext[i].debounced_active ? "1" : "0");
        if (i + 1 < NUM_EXTERNAL_INPUTS) Serial.print(",");
    }
    Serial.print("]");

    // Environmental + GPS snapshot
    // (keep it compact; print 'NA' when invalid)
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


/**
 * @brief Print a cut decision event.
 */
static void debugPrintCutDecision(const CutDecision& d) {
    if (!DEBUG_SERIAL) return;
    Serial.print("CUT DECISION: should_cut=");
    Serial.print(d.should_cut ? "true" : "false");
    Serial.print(" reason=");
    Serial.println((int)d.reason);
}

// -------------------------
// Arduino entry points
// -------------------------

void setup() {

    errorsInit();

    // LED signs-of-life first so power-on always has visible feedback.
    statusLedInit();
    statusLedShowBootSequence();

    // Serial first for bring-up visibility.
    Serial.begin(DEBUG_SERIAL_BAUD);
    delay(50);

    debugPrintln("SkyGuard Cutdown Pro Debug Stream");

    // Load settings (or defaults).
    settingsInit();

    // Init runtime state.
    stateInit(MODE_NORMAL);

    // Init config button system.
    webconfigInit();

    // Hold-at-boot defaults reset (does NOT clear serial number).
    // Note: this function will restart the system
    webconfigCheckHoldAtBoot(HOLD_AT_BOOT_DEFAULTS_MS);

    // Init readings by starting up sensors
    readingsInit();

    const bool skip_boot_charge = webconfigConsumeSkipBootChargeOnce();
    if (!skip_boot_charge) {
        debugPrintln("Boot supercap charge phase...");
        const uint32_t charge_start_ms = millis();
        while ((millis() - charge_start_ms) < BOOT_SUPERCAP_CHARGE_MS) {
            const uint32_t elapsed_ms = millis() - charge_start_ms;
            statusLedShowBootChargeFrame(elapsed_ms);

            if (webconfigPollButton()) {
                return;
            }

            readingsDrainGPS();
            delay(25);
        }
    } else {
        debugPrintln("Skipping boot supercap charge phase after config restart");
    }

    statusLedShowBootInProgress();

    debugPrintln("[BOOT] Boot charge complete");

    // Iridium Modem
    debugPrintln("[BOOT] iridiumInit start");
    iridiumInit();
    debugPrintln("[BOOT] iridiumInit done");

    // Init SD logging
    debugPrintln("[BOOT] sdLogInit start");
    sdLogInit();
    debugPrintln("[BOOT] sdLogInit done");

    // Init cut logic runtime (accumulators, etc).
    debugPrintln("[BOOT] cutLogicInit start");
    cutLogicInit();
    debugPrintln("[BOOT] cutLogicInit done");

    // Init servo mechanism and do the wiggle test
    debugPrintln("[BOOT] servoReleaseInit start");
    servoReleaseInit();
    debugPrintln("[BOOT] servoReleaseInit done");
    debugPrintln("[BOOT] servoReleaseWiggle start");
    servoReleaseWiggle();
    debugPrintln("[BOOT] servoReleaseWiggle done");

    debugPrintln("Setup function complete");

    // Transition from boot indication to normal runtime indication immediately.
    const uint32_t now_ms = millis();
    statusLedUpdate1Hz(now_ms);
    statusLedUpdateFast(now_ms);
}

void loop() {
    const uint32_t now_ms = millis();

    statusLedUpdateFast(now_ms);

    // In normal operation, we poll button quickly. If pressed, webconfigEnter() blocks and
    // will restart on exit, so nothing after this matters in that case.
    if (webconfigPollButton()) {
        return;
    }

    // Drain any incoming serial GPS data
    readingsDrainGPS();

    // 1 Hz update loop for sensors/cut logic/state.
    if (stateTick1Hz(now_ms)) {
        // Update tick-domain runtime state counters.
        stateOn1HzTick(now_ms);

        // Update sensor/input readings once per tick.
        readingsUpdate1Hz(now_ms);

        // Termination detection
        stateUpdateTerminationDetector1Hz(now_ms);

        // Iridium
        iridiumUpdate1Hz(now_ms);

        // Build cut logic inputs from current state + readings.
        CutLogicInputs in;
        readingsFillCutLogicInputs(in);

        in.iridium_remote_cut_request = iridiumGetRemoteCutRequestAndClear();

        // Evaluate cut decision (rule engine + immediate sources).
        const CutDecision d = cutLogicEvaluate1Hz(in);

        debugPrintStatus1Hz();

        sdLogUpdate1Hz(now_ms);

        statusLedUpdate1Hz(now_ms);

        // Actuate cut if requested.
        if (d.should_cut) {
            debugPrintCutDecision(d);

            // Latch in state first (so other modules immediately see cut_fired).
            stateSetCutFired(d.reason, now_ms);

            // Perform physical release (one-shot latched).
            servoReleaseRelease();

            // Refresh LED state immediately so termination is visible without
            // waiting for the next 1 Hz scheduler tick.
            statusLedUpdate1Hz(now_ms);
            statusLedUpdateFast(now_ms);

            // Optional hygiene: if you later keep external inputs latched somewhere,
            // clear them here. In the current 1 Hz quantized model, we do not latch them.
        }

        // TODO (later):
        //  - loggingUpdate1Hz()
        //  - iridiumUpdate1Hz()
        //  - watchdogFeed()
        //  - powerUpdate()
    }
}
