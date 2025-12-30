// cut_logic.cpp
/**
 * @file cut_logic.cpp
 * @brief Implementation of bucket-based cutdown rule engine.
 */

#include "cut_logic.h"
#include "readings.h"
#include "state.h"
#include "servo_release.h"
#include "iridium_link.h"
#include <string.h>
#include <math.h>

// -------------------------
// Internal runtime state
// -------------------------

/**
 * @brief Dwell accumulators (seconds true) for each condition slot.
 *
 * We keep this in cut_logic (not in g_settings) to avoid mixing runtime state into
 * persistent config and to keep web prefill consistent.
 */
static float g_bucketA_true_s[MAX_BUCKET_CONDITIONS];
static float g_bucketB_true_s[MAX_BUCKET_CONDITIONS];

/**
 * @brief Reset internal accumulators.
 */
static void resetAccumulators() {
    memset(g_bucketA_true_s, 0, sizeof(g_bucketA_true_s));
    memset(g_bucketB_true_s, 0, sizeof(g_bucketB_true_s));
}

// -------------------------
// Internal helpers
// -------------------------

/**
 * @brief Compare two floats using a CompareOp.
 *
 * @param lhs Variable value.
 * @param op  CompareOp stored in Condition::op.
 * @param rhs Condition threshold value.
 * @return true if comparison passes; false otherwise.
 */
static bool compare(float lhs, uint8_t op, float rhs) {
    switch ((CompareOp)op) {
        case OP_LT:  return lhs <  rhs;
        case OP_LTE: return lhs <= rhs;
        case OP_EQ:  return lhs == rhs;
        case OP_GTE: return lhs >= rhs;
        case OP_GT:  return lhs >  rhs;
        default:     return false;
    }
}

/**
 * @brief Evaluate a single condition and update its dwell accumulator.
 *
 * Semantics:
 *  - If condition is disabled => ignored by caller (caller should not call).
 *  - If referenced variable is invalid => condition false and accumulator resets to 0.
 *  - If for_seconds == 0 => immediate: satisfied if comparison true.
 *  - Otherwise:
 *      - if comparison true => accumulator += 1.0 (per 1Hz tick)
 *      - if false => accumulator = 0
 *      - satisfied if accumulator >= for_seconds
 *
 * @param c Condition definition.
 * @param var_value Live variable value.
 * @param var_valid Whether variable is valid this tick.
 * @param accum_s Accumulator storage (seconds true).
 * @return true if condition satisfied; false otherwise.
 */
static bool evalCondition1Hz(const Condition& c,
                             float var_value,
                             bool var_valid,
                             float& accum_s) {
    if (!var_valid || !isfinite(var_value) || !isfinite(c.value)) {
        accum_s = 0.0f;
        return false;
    }

    const bool now_true = compare(var_value, c.op, c.value);

    if (!now_true) {
        accum_s = 0.0f;
        return false;
    }

    // Immediate
    if (c.for_seconds == 0) {
        accum_s = 0.0f; // keep it clean (optional)
        return true;
    }

    // Dwell
    accum_s += 1.0f;
    return (accum_s >= (float)c.for_seconds);
}

/**
 * @brief Return true if Bucket A evaluates true.
 *
 * Bucket A semantics:
 *  - ALL enabled must be satisfied.
 *  - If there are zero enabled conditions => treated as TRUE.
 */
static bool evalBucketA1Hz(const CutLogicInputs& in) {
    bool any_enabled = false;

    for (uint8_t i = 0; i < MAX_BUCKET_CONDITIONS; i++) {
        const Condition& c = g_settings.bucketA[i];
        if (!c.enabled) {
            g_bucketA_true_s[i] = 0.0f;
            continue;
        }
        any_enabled = true;

        const uint8_t vid = c.var_id;
        if (vid >= VAR__COUNT) {
            g_bucketA_true_s[i] = 0.0f;
            return false;
        }

        const bool ok = evalCondition1Hz(
            c,
            in.vars[vid],
            in.vars_valid[vid],
            g_bucketA_true_s[i]
        );

        if (!ok) {
            return false; // AND bucket
        }
    }

    return any_enabled ? true : true; // empty => true
}

/**
 * @brief Return true if Bucket B evaluates true.
 *
 * Bucket B semantics:
 *  - ANY enabled must be satisfied.
 *  - If there are zero enabled conditions => treated as FALSE.
 */
static bool evalBucketB1Hz(const CutLogicInputs& in) {
    bool any_enabled = false;
    bool any_true = false;

    for (uint8_t i = 0; i < MAX_BUCKET_CONDITIONS; i++) {
        const Condition& c = g_settings.bucketB[i];
        if (!c.enabled) {
            g_bucketB_true_s[i] = 0.0f;
            continue;
        }
        any_enabled = true;

        const uint8_t vid = c.var_id;
        if (vid >= VAR__COUNT) {
            g_bucketB_true_s[i] = 0.0f;
            continue;
        }

        const bool ok = evalCondition1Hz(
            c,
            in.vars[vid],
            in.vars_valid[vid],
            g_bucketB_true_s[i]
        );

        if (ok) {
            any_true = true;
            // Keep evaluating to update dwell accumulators for other enabled conditions,
            // or return early for efficiency. We choose early return.
            return true;
        }
    }

    if (!any_enabled) return false; // empty => false
    return any_true;
}

/**
 * @brief Return true if global gating conditions allow rule-based cut.
 *
 * These do NOT apply to immediate cuts.
 */
static bool globalsAllowRuleCut(const CutLogicInputs& in) {
    if (g_settings.global_cutdown.require_launch_before_cut && !in.launch_detected) {
        return false;
    }
    if (g_settings.global_cutdown.require_gps_fix_before_cut && !in.gps_fix_present) {
        return false;
    }
    return true;
}

// -------------------------
// Public API
// -------------------------

void cutLogicInit() {
    /**
     * Initialize cut logic runtime state.
     */
    resetAccumulators();
}

void cutLogicResetAccumulators() {
    resetAccumulators();
}

void cutLogicUpdate1Hz(uint32_t now_ms) {
    // Don’t double-fire.
    if (g_state.cut_fired) return;

    CutLogicInputs in;
    readingsFillCutLogicInputs(in);

    // Optional: consume remote-cut request if your design uses one
    // (only if you have this API)
    in.iridium_remote_cut_request = iridiumGetRemoteCutRequestAndClear();

    const CutDecision d = cutLogicEvaluate1Hz(in);

    if (d.should_cut) {
        stateSetCutFired(d.reason, now_ms);
        servoReleaseRelease();
    }
}

void cutLogicInitInputs(CutLogicInputs& out) {
    memset(&out, 0, sizeof(out));
    for (uint8_t i = 0; i < VAR__COUNT; i++) {
        out.vars[i] = 0.0f;
        out.vars_valid[i] = false;
    }
    for (uint8_t i = 0; i < NUM_EXTERNAL_INPUTS; i++) {
        out.external_cut_active[i] = false;
    }

    out.launch_detected = false;
    out.gps_fix_present = false;
    out.iridium_remote_cut_request = false;
}

CutDecision cutLogicEvaluate1Hz(const CutLogicInputs& in) {
    CutDecision d;
    d.should_cut = false;
    d.reason = CUT_REASON_NONE;

    // If already cut, never request another cut.
    if (g_state.cut_fired) {
        return d;
    }

    // -------------------------
    // Priority 1: Immediate cuts
    // -------------------------

    // External inputs (already debounced upstream; we just honor enable and polarity upstream)
    for (uint8_t i = 0; i < NUM_EXTERNAL_INPUTS; i++) {
        if (g_settings.external_inputs[i].enabled && in.external_cut_active[i]) {
            d.should_cut = true;
            d.reason = CUT_REASON_EXTERNAL_INPUT;
            return d;
        }
    }

    // Iridium remote cut request (token validation occurs in iridium_link)
    if (g_settings.iridium.enabled &&
        g_settings.iridium.cutdown_on_command &&
        in.iridium_remote_cut_request) {
        d.should_cut = true;
        d.reason = CUT_REASON_IRIDIUM_REMOTE;
        return d;
    }

    // -------------------------
    // Priority 2: Rule-based cut
    // -------------------------
    if (!globalsAllowRuleCut(in)) {
        // If globals block, also reset dwell timers so you don't "store up" dwell time
        // while blocked. This is a design choice; conservative behavior.
        resetAccumulators();
        return d;
    }

    const bool a_ok = evalBucketA1Hz(in);
    const bool b_ok = evalBucketB1Hz(in);

    if (a_ok && b_ok) {
        d.should_cut = true;
        d.reason = CUT_REASON_BUCKET_LOGIC;
        return d;
    }

    return d;
}
