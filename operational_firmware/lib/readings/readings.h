// readings.h
#pragma once

/**
 * @file readings.h
 * @brief Central sensor/input snapshot for SkyGuard Cutdown Pro.
 *
 * Responsibilities:
 *  - Read and store latest sensor values (GPS, environmental, etc.).
 *  - Read and store "user message" inputs (external optoisolated inputs).
 *  - Provide a single 1 Hz update entry point.
 *  - Provide mapping to CutLogicInputs (vars[]/valid[] + flags).
 *
 * Design notes:
 *  - This module is intentionally the "one place" to inspect what the firmware believes
 *    the world looks like right now.
 *  - All values are stored with explicit validity flags.
 *  - External inputs are sampled at 1 Hz (per your preference).
 */

#include <stdint.h>
#include <stdbool.h>

#include "settings.h"
#include "cut_logic.h"

/**
 * @struct ExternalInputReading
 * @brief Runtime interpretation of one optoisolated input.
 *
 * Debounce model (1 Hz quantized):
 *  - If raw active on a tick: active_accum_ms += 1000
 *  - Else: active_accum_ms = 0
 *  - debounced_active = (active_accum_ms >= debounce_ms)
 */
struct ExternalInputReading {
    bool     raw_active;        ///< Raw active after polarity mapping.
    bool     debounced_active;  ///< True if considered active after debounce.
    uint32_t active_accum_ms;   ///< Accumulator (quantized) used for debounce.
};

/**
 * @struct Readings
 * @brief Latest sensor/input snapshot.
 *
 * This is runtime state only (not persisted).
 */
struct Readings {
    // --- GPS (v1 placeholders; fill when GPS driver exists) ---
    bool  gps_fix_valid;
    bool  gps_fix;          ///< 0/1 meaning no fix / fix.
    bool  gps_lat_valid;
    float gps_lat_deg;
    bool  gps_lon_valid;
    float gps_lon_deg;
    bool  gps_alt_valid;
    float gps_alt_m;

    // --- Environmental (v1 placeholders; fill when sensor driver exists) ---
    bool  pressure_valid;
    float pressure_hpa;
    bool  temp_valid;
    float temp_c;
    bool  humidity_valid;
    float humidity_pct;

    // --- External inputs (user message channels) ---
    ExternalInputReading ext[NUM_EXTERNAL_INPUTS];
};

/// Global singleton readings instance.
extern Readings g_readings;

/**
 * @brief Initialize readings subsystem.
 *
 * Sets validity flags false and initializes external input accumulators.
 * Call once at boot.
 */
void readingsInit();

/**
 * @brief Update all readings once per 1 Hz tick.
 *
 * Reads external inputs and (later) sensors. Also a natural place for launch detection
 * logic once GPS/pressure data exists.
 *
 * @param now_ms Current millis().
 */
void readingsUpdate1Hz(uint32_t now_ms);

/**
 * @brief Populate CutLogicInputs from g_state + g_readings.
 *
 * This sets:
 *  - vars[] / vars_valid[]
 *  - in.launch_detected, in.gps_fix_present
 *  - in.external_cut_active[] (from debounced external inputs)
 *
 * Caller can then also set in.iridium_remote_cut_request based on iridium_link.
 *
 * @param out CutLogicInputs to fill (will be fully initialized inside).
 */
void readingsFillCutLogicInputs(CutLogicInputs& out);

void readingsDrainGPS();