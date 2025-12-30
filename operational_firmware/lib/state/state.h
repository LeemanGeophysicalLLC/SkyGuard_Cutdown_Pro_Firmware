// state.h
#pragma once

/**
 * @file state.h
 * @brief Runtime state for SkyGuard Cutdown Pro (non-persistent).
 *
 * Design goals:
 *  - Keep all mutable runtime variables in one place.
 *  - Deterministic timing: support a 1 Hz “tick” scheduler.
 *  - Store one-shot latches (launch detected, cut fired).
 *  - Track high-level flight state and system mode (orthogonal).
 *
 * Non-goals:
 *  - Persistent settings (see settings.h).
 *  - Hardware control (servo, sensors, iridium, etc).
 */

#include <stdint.h>
#include <stdbool.h>


/**
 * @enum FlightState
 * @brief Physical reality state machine (what the balloon is doing).
 *
 * Notes:
 *  - Flight state is orthogonal to system mode (MODE_NORMAL vs MODE_CONFIG).
 *  - Entering config mode does NOT change flight state; it just pauses autonomous behavior.
 */
enum FlightState : uint8_t {
    FLIGHT_GROUND = 0,     ///< On the ground / pre-launch.
    FLIGHT_IN_FLIGHT,      ///< Launch detected and still flying.
    FLIGHT_TERMINATED      ///< Descent/termination started (cut fired OR pop detected).

};

/**
 * @enum SystemMode
 * @brief Firmware behavior mode (what the MCU is doing).
 *
 * Notes:
 *  - MODE_CONFIG pauses autonomous logic and starts WiFi AP + web UI.
 *  - MODE_NORMAL is autonomous operation (WiFi off, low power behavior allowed).
 */
enum SystemMode : uint8_t {
    MODE_NORMAL = 0,       ///< Autonomous flight firmware.
    MODE_CONFIG            ///< Config mode (WiFi AP + web server), flight logic paused.
};

/**
 * @enum CutReason
 * @brief Why the cut fired (latched once per power cycle).
 */
enum CutReason : uint8_t {
    CUT_REASON_NONE = 0,       ///< No cut has occurred.
    CUT_REASON_BUCKET_LOGIC,   ///< Bucket A/B rule engine fired.
    CUT_REASON_EXTERNAL_INPUT, ///< External optoisolated input forced cut.
    CUT_REASON_IRIDIUM_REMOTE, ///< Iridium command forced cut.
    CUT_REASON_MANUAL          ///< Manual cut (e.g., web UI button in config mode).
};

/**
 * @struct Scheduler1Hz
 * @brief Helper state for generating a stable 1 Hz tick.
 *
 * Concept:
 *  - Call stateTick1Hz(now_ms) in loop().
 *  - It returns true exactly once per second (as best as possible) based on millis().
 *  - We use a deadline-based approach to reduce drift.
 */
struct Scheduler1Hz {
    uint32_t next_tick_ms;     ///< Next scheduled tick time (millis).
    bool     initialized;      ///< Has next_tick_ms been initialized yet?
    uint16_t last_elapsed_s;   ///< Seconds elapsed since last emitted tick (>=1 when a tick is emitted).
};

/**
 * @struct RuntimeState
 * @brief The global runtime state struct (one instance).
 *
 * Timing:
 *  - power_on_ms is when stateInit() ran (approx boot time).
 *  - t_power_s increments on each 1 Hz tick (derived, not from millis()).
 *
 * Launch:
 *  - launch_detected is a latch set by sensors/launch detection logic.
 *  - launch_ms captures the moment of launch detection.
 *  - t_launch_s is seconds since launch (0 if not launched).
 *
 * Cut:
 *  - cut_fired is a latch; once true, it stays true until reboot.
 *  - cut_reason stores why.
 *  - cut_ms captures time of cut.
 */
struct RuntimeState {
    // High-level mode/state
    FlightState flight_state;
    SystemMode  system_mode;

    // 1 Hz scheduler
    Scheduler1Hz sched_1hz;

    // Boot timing
    uint32_t power_on_ms;

    // Derived timekeeping (1 Hz domain)
    uint32_t t_power_s;     ///< Seconds since boot (increments with 1 Hz tick).

    // Launch latch + timing
    bool     launch_detected;
    uint32_t launch_ms;
    uint32_t t_launch_s;    ///< Seconds since launch (0 if not launched).

    // Cut latch + timing
    bool      cut_fired;
    CutReason cut_reason;
    uint32_t  cut_ms;

    // Termination latch + timing (cut OR balloon pop / descent termination)
    bool     terminated;        ///< Latched once when we decide the flight is terminated.
    uint32_t terminated_ms;     ///< millis() at termination latch.
    uint32_t t_terminated_s;    ///< Seconds since termination (0 if not terminated).

    float peak_alt_m;          // highest GPS altitude seen since in-flight
    float min_pressure_hpa;    // lowest pressure seen since in-flight

    uint16_t descent_count_s;  // consecutive seconds condition held

};

/**
 * @brief Global singleton runtime state.
 *
 * This is intentionally a plain struct for readability and debugging.
 */
extern RuntimeState g_state;

/**
 * @brief Initialize runtime state to safe defaults.
 *
 * Call once at boot before any modules use g_state.
 *
 * @param initial_mode Usually MODE_NORMAL; can be MODE_CONFIG for early entry.
 */
void stateInit(SystemMode initial_mode);

/**
 * @brief Generate a stable 1 Hz tick.
 *
 * Call frequently (each loop iteration). Returns true once per second.
 *
 * Implementation details:
 *  - Uses g_state.sched_1hz.
 *  - Deadline-based ticking reduces drift compared to "now - last >= 1000".
 *
 * @param now_ms Current millis().
 * @return true if a 1 Hz tick occurred; false otherwise.
 */
bool stateTick1Hz(uint32_t now_ms);

/**
 * @brief Update derived time fields once per 1 Hz tick.
 *
 * Call this exactly when stateTick1Hz() returns true.
 *
 * @param now_ms Current millis().
 */
void stateOn1HzTick(uint32_t now_ms);

/**
 * @brief Set launch-detected latch (one-shot).
 *
 * If launch is already detected, this does nothing.
 *
 * @param now_ms Current millis().
 */
void stateSetLaunchDetected(uint32_t now_ms);

/**
 * @brief Latch a cut event and transition flight state.
 *
 * This does NOT move the servo by itself — it only records the event.
 * The cut_logic / servo_release modules execute the physical cut.
 *
 * If a cut is already fired, this does nothing.
 *
 * @param reason Why the cut fired.
 * @param now_ms Current millis().
 */
void stateSetCutFired(CutReason reason, uint32_t now_ms);

/**
 * @brief Change system mode (MODE_NORMAL / MODE_CONFIG).
 *
 * This does not touch flight state.
 *
 * @param mode New mode.
 */
void stateSetSystemMode(SystemMode mode);

/**
 * @brief Latch flight termination (cut OR pop detected) and transition flight state.
 *
 * One-shot: does nothing if already terminated.
 * Records termination time, sets flight_state = FLIGHT_TERMINATED.
 *
 * @param now_ms Current millis().
 */
void stateSetTerminated(uint32_t now_ms);

void stateUpdateTerminationDetector1Hz(uint32_t now_ms);