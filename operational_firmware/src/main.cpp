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

    // Serial first for bring-up visibility.
    Serial.begin(DEBUG_SERIAL_BAUD);
    delay(50);

    debugPrintln("SkyGuard Cutdown Pro Debug Stream");

    // LED Setup
    statusLedInit();

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

    // Iridium Modem
    iridiumInit();

    // Init SD logging
    sdLogInit();

    // Init cut logic runtime (accumulators, etc).
    cutLogicInit();

    // Init servo mechanism and do the wiggle test
    servoReleaseInit();
    servoReleaseWiggle();

    debugPrintln("Setup function complete");
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
