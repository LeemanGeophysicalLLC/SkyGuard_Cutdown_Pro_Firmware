// cut_logic.h
#pragma once

/**
 * @file cut_logic.h
 * @brief Cut rule engine for SkyGuard Cutdown Pro.
 *
 * Responsibilities:
 *  - Evaluate immediate cut sources (external inputs, Iridium remote cut request).
 *  - Evaluate rule-based cut using Bucket A (ALL) and Bucket B (ANY) conditions.
 *  - Enforce global cut gating (require launch and/or GPS fix) for rule-based cuts only.
 *
 * Non-responsibilities:
 *  - Reading sensors (sensors.h).
 *  - Debouncing external inputs (handled by external input module or caller).
 *  - Parsing/validating Iridium commands (iridium_link.h).
 *  - Actuating the servo (servo_release.h).
 *  - Persisting configuration (settings.h).
 *
 * Usage pattern (1 Hz):
 *  - Call cutLogicInit() at boot.
 *  - Each 1 Hz tick: build CutLogicInputs from latest sensor/state values.
 *  - Call cutLogicEvaluate1Hz(in).
 *  - If decision.should_cut == true, main should:
 *      - stateSetCutFired(decision.reason, now_ms)
 *      - servoReleaseRelease()
 */

#include <stdint.h>
#include <stdbool.h>

#include "settings.h" // VariableId, Condition, SystemConfig
#include "state.h"    // CutReason

/**
 * @struct CutLogicInputs
 * @brief Snapshot of values required to evaluate cut logic on a 1 Hz tick.
 *
 * Notes:
 *  - vars[] contains numeric values indexed by VariableId.
 *  - If a variable is not available, set vars_valid[var_id] = false and the engine
 *    will treat any condition referencing it as false (and reset dwell time).
 */
struct CutLogicInputs {
    // Variable values + validity
    float vars[VAR__COUNT];
    bool  vars_valid[VAR__COUNT];

    // Global state flags (usually derived from g_state + sensors)
    bool launch_detected;     ///< True if launch has been detected (latch).
    bool gps_fix_present;     ///< True if GPS fix is present (0/1 is fine).

    // Immediate cut requests (already debounced/validated upstream)
    bool external_cut_active[NUM_EXTERNAL_INPUTS]; ///< True if each ext input is active this tick.
    bool iridium_remote_cut_request;               ///< True if remote cut command is requested this tick.
};

/**
 * @struct CutDecision
 * @brief Result of one cut logic evaluation.
 */
struct CutDecision {
    bool      should_cut; ///< True if cut should fire now.
    CutReason reason;     ///< Reason for cut (meaningful only if should_cut).
};

/**
 * @brief Initialize cut logic runtime state.
 *
 * Clears dwell accumulators for all conditions.
 * Call once at boot (or after leaving config mode if you want a clean slate).
 */
void cutLogicInit();

/**
 * @brief Reset dwell accumulators without changing any configuration.
 *
 * Useful if you want to “restart” rule evaluation without rebooting.
 */
void cutLogicResetAccumulators();

/**
 * @brief Evaluate cut logic once per 1 Hz tick.
 *
 * Priority:
 *  1) If g_state.cut_fired is already true, returns should_cut=false always.
 *  2) Immediate cut sources:
 *      - external input active AND enabled in settings
 *      - Iridium remote cut request AND enabled in settings
 *  3) Rule-based cut:
 *      - global toggles satisfied (launch/fix gates)
 *      - Bucket A: all enabled conditions true (empty => true)
 *      - Bucket B: any enabled condition true (empty => false)
 *
 * @param in Snapshot values for this tick.
 * @return CutDecision result.
 */
CutDecision cutLogicEvaluate1Hz(const CutLogicInputs& in);

/**
 * @brief Helper to fill CutLogicInputs with safe defaults.
 *
 * Sets vars_valid[] = false, zeroes vars[], and clears immediate cut requests.
 *
 * @param out Inputs struct to initialize.
 */
void cutLogicInitInputs(CutLogicInputs& out);
