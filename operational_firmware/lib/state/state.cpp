// state.cpp
/**
 * @file state.cpp
 * @brief Runtime state implementation.
 */

#include "state.h"
#include "readings.h"
#include "settings.h"

#include <Arduino.h>
#include <string.h>

// Global instance
RuntimeState g_state;

/**
 * @brief Reset scheduler struct.
 */
static void schedulerReset(Scheduler1Hz& s) {
    s.initialized = false;
    s.next_tick_ms = 0;
    s.last_elapsed_s = 0;
}

void stateInit(SystemMode initial_mode) {
    /**
     * Initialize all runtime state fields.
     */
    memset(&g_state, 0, sizeof(g_state));

    g_state.system_mode = initial_mode;
    g_state.flight_state = FLIGHT_GROUND;

    g_state.power_on_ms = millis();

    g_state.t_power_s = 0;

    g_state.launch_detected = false;
    g_state.launch_ms = 0;
    g_state.t_launch_s = 0;

    // Termination latch (cut OR balloon pop / descent termination)
    g_state.terminated = false;
    g_state.terminated_ms = 0;
    g_state.t_terminated_s = 0;

    g_state.cut_fired = false;
    g_state.cut_reason = CUT_REASON_NONE;
    g_state.cut_ms = 0;

    schedulerReset(g_state.sched_1hz);
}

bool stateTick1Hz(uint32_t now_ms) {
    /**
     * Deadline-based 1 Hz tick generation, but with a single emitted tick per call.
     *
     * If the loop stalls for N seconds, we emit ONE tick and record N seconds
     * elapsed in sched_1hz.last_elapsed_s.
     */
    Scheduler1Hz& s = g_state.sched_1hz;

    if (!s.initialized) {
        s.initialized = true;
        s.next_tick_ms = now_ms + 1000;
        s.last_elapsed_s = 0;
        return false;
    }

    // Not yet time for the next tick.
    if ((int32_t)(now_ms - s.next_tick_ms) < 0) {
        return false;
    }

    // At least 1 second has elapsed since the scheduled deadline.
    const uint32_t elapsed_s_u32 =
        1UL + (uint32_t)(now_ms - s.next_tick_ms) / 1000UL;

    // Advance deadline by the elapsed amount to minimize drift.
    s.next_tick_ms += elapsed_s_u32 * 1000UL;

    // Store elapsed seconds (clamp to uint16_t).
    s.last_elapsed_s = (elapsed_s_u32 > 0xFFFFUL) ? 0xFFFFU : (uint16_t)elapsed_s_u32;

    return true;
}

void stateOn1HzTick(uint32_t now_ms) {
    (void)now_ms;
    const uint16_t dt_s =
    (g_state.sched_1hz.last_elapsed_s > 0) ? g_state.sched_1hz.last_elapsed_s : 1;


    /**
     * Update derived time counters in the 1 Hz domain.
     *
     * We explicitly increment t_power_s rather than deriving from millis()
     * so the rest of firmware can be strictly “tick driven”.
     */
    g_state.t_power_s += dt_s;

    if (g_state.launch_detected) {
        g_state.t_launch_s += dt_s;
    } else {
        g_state.t_launch_s = 0;
    }

    if (g_state.terminated) {
        g_state.t_terminated_s += dt_s;
    } else {
        g_state.t_terminated_s = 0;
    }

    // Flight state:
    // - Termination dominates (cut OR pop detected)
    // - Otherwise launch_detected indicates in-flight
    if (g_state.terminated) {
        g_state.flight_state = FLIGHT_TERMINATED;
    } else if (g_state.launch_detected) {
        g_state.flight_state = FLIGHT_IN_FLIGHT;

    } else {
        g_state.flight_state = FLIGHT_GROUND;
    }
}

void stateUpdateTerminationDetector1Hz(uint32_t now_ms) {
    if (g_state.terminated) return;
    if (g_state.flight_state != FLIGHT_IN_FLIGHT) return;

    const auto &tcfg = g_settings.term;   // <-- settings, not state
    if (!tcfg.enabled) return;

    bool gps_condition = false;
    bool pressure_condition = false;

    // --- GPS peak-drop path ---
    if (tcfg.use_gps && g_readings.gps_fix_valid && g_readings.gps_alt_valid) {
        const float alt_m = g_readings.gps_alt_m;

        if (alt_m > g_state.peak_alt_m) g_state.peak_alt_m = alt_m;

        const float drop_m = g_state.peak_alt_m - alt_m;
        if (drop_m >= tcfg.gps_drop_m) gps_condition = true;
    }

    // --- Pressure min-rise path ---
    if (tcfg.use_pressure && g_readings.pressure_valid) {
        const float p_hpa = g_readings.pressure_hpa;

        if (p_hpa < g_state.min_pressure_hpa) g_state.min_pressure_hpa = p_hpa;

        const float rise_hpa = p_hpa - g_state.min_pressure_hpa;
        if (rise_hpa >= tcfg.pressure_rise_hpa) pressure_condition = true;
    }

    const bool descent_now = gps_condition || pressure_condition;

    if (descent_now) {
        if (g_state.descent_count_s < 0xFFFF) g_state.descent_count_s++;
    } else {
        g_state.descent_count_s = 0;
    }

    if (g_state.descent_count_s >= tcfg.sustain_s) {
        stateSetTerminated(now_ms);
    }
}


void stateSetLaunchDetected(uint32_t now_ms) {
    /**
     * One-shot latch: only the first detection matters.
     */
    if (g_state.launch_detected) return;

    g_state.launch_detected = true;
    g_state.launch_ms = now_ms;
    g_state.t_launch_s = 0; // start counting from next 1Hz tick
    g_state.peak_alt_m = -1e9f;
    g_state.min_pressure_hpa =  1e9f;
    g_state.descent_count_s = 0;
}

void stateSetTerminated(uint32_t now_ms) {
    /**
     * One-shot latch: only the first termination matters.
     *
     * Termination means: we are descending / done with "in-flight" phase,
     * whether due to a cut firing OR a natural balloon pop.
     */
    if (g_state.terminated) return;

    g_state.terminated = true;
    g_state.terminated_ms = now_ms;
    g_state.t_terminated_s = 0;

    // Immediate high-level transition (also enforced in stateOn1HzTick).
    g_state.flight_state = FLIGHT_TERMINATED;
}

void stateSetCutFired(CutReason reason, uint32_t now_ms) {
    /**
     * One-shot latch: only first cut matters.
     */
    if (g_state.cut_fired) return;

    g_state.cut_fired = true;
    g_state.cut_reason = reason;
    g_state.cut_ms = now_ms;

    // A cut always implies termination.
    stateSetTerminated(now_ms);
}

void stateSetSystemMode(SystemMode mode) {
    g_state.system_mode = mode;
}
