// readings.cpp
/**
 * @file readings.cpp
 * @brief Implementation of central sensor/input snapshot.
 */

#include "readings.h"

#include "pins.h"
#include "state.h"
#include "debug.h"
#include "errors.h"
#include "project_config.h"

#include <Arduino.h>
#include <string.h>
#include <Wire.h>
#include <math.h>

#include "Adafruit_Sensor.h"
#include "Adafruit_BME680.h"
#include "TinyGPS++.h"
#include "SparkFun_u-blox_GNSS_Arduino_Library.h"

Readings g_readings;
Adafruit_BME680 bme(&Wire);
TinyGPSPlus gps;
SFE_UBLOX_GNSS myGNSS;

// -------------------------
// Launch detection runtime
// -------------------------
static bool  s_launch_base_gps_valid = false;
static float s_launch_base_gps_alt_m = 0.0f;

static bool  s_launch_base_baro_valid = false;
static float s_launch_base_pressure_hpa = 0.0f;

static uint8_t s_launch_persist_s = 0;

/**
 * @brief Initialize and configure the u-blox GPS module.
 *
 * This function configures the u-blox module over I2C (SparkFun GNSS lib),
 * then relies on NMEA over UART1 at 115200 for TinyGPS++ parsing.
 *
 * IMPORTANT:
 * - This function should not hard-halt the system. It should set ERR_GPS and return false on failure.
 * - ERR_GPS is intended to mean "GPS not talking / not configured / dead", NOT "no fix yet".
 */
bool setupGPS()
{
    if (!myGNSS.begin()) {
        debugPrintln("GPS not detected. Check I2C wiring.");
        errorSet(ERR_GPS);
        return false;
    }

    debugPrintln("Configuring GPS...");

    // Set dynamic model to AIRBORNE <4g>
    if (!myGNSS.setDynamicModel(DYN_MODEL_AIRBORNE4g)) {
        debugPrintln("Failed to set dynamic model");
        errorSet(ERR_GPS);
        return false;
    }

    // Set UART1 baud rate to 115200
    myGNSS.setSerialRate(115200, COM_PORT_UART1); // No return value
    debugPrintln("Set UART1 baud rate to 115200.");

    // Ensure UART is configured to receive NMEA for TinyGPS++
    // NOTE: begin(baud, config, rxPin, txPin) -> RX then TX.
    Serial2.begin(115200, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

    // Set navigation rate to 1 Hz
    if (!myGNSS.setNavigationFrequency(1)) {
        debugPrintln("Failed to set navigation frequency");
        errorSet(ERR_GPS);
        return false;
    }

    // Disable all common NMEA messages
    myGNSS.disableNMEAMessage(UBX_NMEA_GLL, COM_PORT_UART1);
    myGNSS.disableNMEAMessage(UBX_NMEA_GGA, COM_PORT_UART1);
    myGNSS.disableNMEAMessage(UBX_NMEA_GSA, COM_PORT_UART1);
    myGNSS.disableNMEAMessage(UBX_NMEA_GSV, COM_PORT_UART1);
    myGNSS.disableNMEAMessage(UBX_NMEA_VTG, COM_PORT_UART1);
    myGNSS.disableNMEAMessage(UBX_NMEA_GNS, COM_PORT_UART1);

    // Enable RMC and ZDA; keep GGA for altitude
    myGNSS.enableNMEAMessage(UBX_NMEA_RMC, COM_PORT_UART1);
    myGNSS.enableNMEAMessage(UBX_NMEA_ZDA, COM_PORT_UART1);
    myGNSS.enableNMEAMessage(UBX_NMEA_GGA, COM_PORT_UART1);

    UBX_CFG_TP5_data_t timePulseSettings;
    memset(&timePulseSettings, 0, sizeof(UBX_CFG_TP5_data_t)); // Clear struct

    timePulseSettings.tpIdx = 0;                     // TIMEPULSE pin 0
    timePulseSettings.version = 0x01;
    timePulseSettings.flags.bits.active = 1;         // Enable output
    timePulseSettings.flags.bits.lockedOtherSet = 1; // Align to top of second
    timePulseSettings.flags.bits.isFreq = 1;         // Frequency mode
    timePulseSettings.flags.bits.isLength = 1;       // Length is valid
    timePulseSettings.freqPeriod = 1;                // 1 Hz
    timePulseSettings.freqPeriodLock = 1;
    timePulseSettings.pulseLenRatio = 100000;        // 100 ms = 100,000 ns
    timePulseSettings.pulseLenRatioLock = 100000;

    if (!myGNSS.setTimePulseParameters(&timePulseSettings)) {
        debugPrintln("Failed to configure time pulse!");
        // Not necessarily fatal for basic operation, but indicates GPS config problems.
        errorSet(ERR_GPS);
    } else {
        debugPrintln("Time pulse configured.");
    }

    // Save settings
    if (!myGNSS.saveConfiguration()) {
        debugPrintln("Failed to save configuration!");
        errorSet(ERR_GPS);
        return false;
    } else {
        debugPrintln("GPS configuration saved.");
    }

    debugPrintln("Setup complete. GPS will output RMC/ZDA/GGA at 115200 baud over UART1.");

    // GPS configured successfully. Do not clear ERR_GPS here; ERR_GPS should represent
    // comm-health, and is best maintained by your fast UART drain/heartbeat logic.
    return true;
}

void readingsDrainGPS() {
    while (Serial2.available() > 0) {
        char c = (char)Serial2.read();
        gps.encode(c);
    }
}

/**
 * @brief Return GPIO pin for external input index.
 */
static uint8_t extPinForIndex(uint8_t idx) {
    return (idx == 0) ? PIN_EXT1 : PIN_EXT2;
}

/**
 * @brief Convert pin level to "active" based on polarity.
 */
static bool levelToActive(int pin_level, bool active_high) {
    if (active_high) return (pin_level == HIGH);
    return (pin_level == LOW);
}

/**
 * @brief Clear all sensor validity flags and values to safe defaults.
 */
static void clearSensors(Readings& r) {
    r.gps_fix_valid = false; r.gps_fix = false;
    r.gps_lat_valid = false; r.gps_lat_deg = 0.0f;
    r.gps_lon_valid = false; r.gps_lon_deg = 0.0f;
    r.gps_alt_valid = false; r.gps_alt_m = 0.0f;

    r.pressure_valid = false; r.pressure_hpa = 0.0f;
    r.temp_valid = false; r.temp_c = 0.0f;
    r.humidity_valid = false; r.humidity_pct = 0.0f;
}

/**
 * @brief Initialize external input runtime state.
 */
static void initExternalInputs(Readings& r) {
    // Configure GPIO. Because these are optoisolated, we default to plain INPUT.
    pinMode(PIN_EXT1, INPUT);
    pinMode(PIN_EXT2, INPUT);

    for (uint8_t i = 0; i < NUM_EXTERNAL_INPUTS; i++) {
        r.ext[i].raw_active = false;
        r.ext[i].debounced_active = false;
        r.ext[i].active_accum_ms = 0;
    }
}

static void resetLaunchDetectionRuntime() {
    s_launch_base_gps_valid = false;
    s_launch_base_gps_alt_m = 0.0f;

    s_launch_base_baro_valid = false;
    s_launch_base_pressure_hpa = 0.0f;

    s_launch_persist_s = 0;
}

bool setupEnvironmental()
{
    // Startup the BME environmental sensor
    if (!bme.begin()) {
        debugPrintln("Could not find a valid BME680 sensor, check wiring!");
        errorSet(ERR_ENV_SENSOR);
        return false;
    }

    // Set up oversampling and filter initialization
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150); // 320*C for 150 ms

    return true;
}

void readingsInit() {
    memset(&g_readings, 0, sizeof(g_readings));
    clearSensors(g_readings);
    initExternalInputs(g_readings);

    resetLaunchDetectionRuntime();

    // Configure GPS + environmental sensor. These should set errors and return false on failure.
    setupGPS();
    setupEnvironmental();
}

/**
 * @brief Update one external input channel at 1 Hz.
 */
static void updateExternalInput1Hz(uint8_t idx) {
    const bool enabled = g_settings.external_inputs[idx].enabled;
    if (!enabled) {
        g_readings.ext[idx].raw_active = false;
        g_readings.ext[idx].debounced_active = false;
        g_readings.ext[idx].active_accum_ms = 0;
        return;
    }

    const uint8_t pin = extPinForIndex(idx);
    const bool active_high = g_settings.external_inputs[idx].active_high;
    const uint16_t debounce_ms = g_settings.external_inputs[idx].debounce_ms;

    const bool raw_active = levelToActive(digitalRead(pin), active_high);
    g_readings.ext[idx].raw_active = raw_active;

    if (raw_active) {
        // 1 Hz quantized accumulation
        g_readings.ext[idx].active_accum_ms += 1000;
        // Prevent wrap if someone holds it for days
        if (g_readings.ext[idx].active_accum_ms > 60000UL) {
            g_readings.ext[idx].active_accum_ms = 60000UL;
        }
    } else {
        g_readings.ext[idx].active_accum_ms = 0;
    }

    g_readings.ext[idx].debounced_active =
        (g_readings.ext[idx].active_accum_ms >= (uint32_t)debounce_ms);
}

void readingsUpdate1Hz(uint32_t now_ms) {
    // ---- External inputs ----
    for (uint8_t i = 0; i < NUM_EXTERNAL_INPUTS; i++) {
        updateExternalInput1Hz(i);
    }

    // ---- Environmental sensor (BME680) ----
    if (!bme.performReading()) {
        errorSet(ERR_ENV_SENSOR);
        g_readings.temp_valid = false;
        g_readings.humidity_valid = false;
        g_readings.pressure_valid = false;
    } else {
        errorClear(ERR_ENV_SENSOR);
        g_readings.temp_c = bme.temperature;
        g_readings.temp_valid = true;
        g_readings.humidity_pct = bme.humidity;
        g_readings.humidity_valid = true;
        g_readings.pressure_hpa = bme.pressure / 100.0f;
        g_readings.pressure_valid = true;
    }

    // ---- GPS snapshot (TinyGPS++) ----
    const unsigned long maxAge = (unsigned long)GPS_MAX_FIELD_AGE_MS;

    const bool time_ok =
        gps.time.isValid() &&
        gps.date.isValid() &&
        (gps.time.age() < maxAge) &&
        (gps.date.age() < maxAge);

    const bool loc_ok =
        gps.location.isValid() &&
        (gps.location.age() < maxAge);

    // FIX VALID means "GPS subsystem is alive/talking" (not "has fix").
    // ERR_GPS should be maintained by your comm-health logic elsewhere.
    g_readings.gps_fix_valid = !errorIsActive(ERR_GPS);

    // This is your "has usable fix right now" flag (BLUE until true if required)
    g_readings.gps_fix = loc_ok;

    if (loc_ok) {
        g_readings.gps_lat_deg = (float)gps.location.lat();
        g_readings.gps_lon_deg = (float)gps.location.lng();
        g_readings.gps_lat_valid = true;
        g_readings.gps_lon_valid = true;
    } else {
        g_readings.gps_lat_valid = false;
        g_readings.gps_lon_valid = false;
    }

    if (gps.altitude.isValid() && (gps.altitude.age() < maxAge)) {
        g_readings.gps_alt_m = (float)gps.altitude.meters();
        g_readings.gps_alt_valid = true;
    } else {
        g_readings.gps_alt_valid = false;
    }

    // time_ok is currently not used by SkyGuard logic, but retained for future expansion.
    (void)time_ok;

    // ---- Launch detection (v1) ----
    // Rules:
    // - only latch once
    // - do NOT latch launch until startup is healthy (no critical errors)
    // - trigger if either:
    //     * GPS altitude rises by >= LAUNCH_GPS_ALT_RISE_M above baseline
    //     * Baro pressure drops by >= LAUNCH_PRESSURE_DROP_HPA below baseline
    // - require persistence: LAUNCH_PERSIST_REQUIRED_S consecutive 1 Hz ticks
    if (!g_state.launch_detected) {
        if (errorsAnyCriticalActive()) {
            // Startup isn't healthy yet; don't accumulate toward launch.
            s_launch_persist_s = 0;
        } else {
            // Capture baselines when each sensor first becomes valid.
            if (!s_launch_base_gps_valid && g_readings.gps_alt_valid) {
                s_launch_base_gps_alt_m = g_readings.gps_alt_m;
                s_launch_base_gps_valid = true;
            }
            if (!s_launch_base_baro_valid && g_readings.pressure_valid) {
                s_launch_base_pressure_hpa = g_readings.pressure_hpa;
                s_launch_base_baro_valid = true;
            }

            bool launch_candidate = false;

            if (s_launch_base_gps_valid && g_readings.gps_alt_valid) {
                const float d_alt_m = g_readings.gps_alt_m - s_launch_base_gps_alt_m;
                if (d_alt_m >= LAUNCH_GPS_ALT_RISE_M) {
                    launch_candidate = true;
                }
            }

            if (s_launch_base_baro_valid && g_readings.pressure_valid) {
                const float d_p_hpa = s_launch_base_pressure_hpa - g_readings.pressure_hpa;
                if (d_p_hpa >= LAUNCH_PRESSURE_DROP_HPA) {
                    launch_candidate = true;
                }
            }

            if (launch_candidate) {
                if (s_launch_persist_s < 255) {
                    s_launch_persist_s++;
                }
            } else {
                s_launch_persist_s = 0;
            }

            if (s_launch_persist_s >= LAUNCH_PERSIST_REQUIRED_S) {
                stateSetLaunchDetected(now_ms);
            }
        }
    }
}

void readingsFillCutLogicInputs(CutLogicInputs& out) {
    cutLogicInitInputs(out);

    // --- Time variables (always valid in our tick domain) ---
    out.vars_valid[VAR_T_POWER_S] = true;
    out.vars[VAR_T_POWER_S] = (float)g_state.t_power_s;

    out.vars_valid[VAR_T_LAUNCH_S] = true;
    out.vars[VAR_T_LAUNCH_S] = (float)g_state.t_launch_s;

    // --- GPS variables ---
    if (g_readings.gps_alt_valid) {
        out.vars_valid[VAR_GPS_ALT_M] = true;
        out.vars[VAR_GPS_ALT_M] = g_readings.gps_alt_m;
    }
    if (g_readings.gps_lat_valid) {
        out.vars_valid[VAR_GPS_LAT_DEG] = true;
        out.vars[VAR_GPS_LAT_DEG] = g_readings.gps_lat_deg;
    }
    if (g_readings.gps_lon_valid) {
        out.vars_valid[VAR_GPS_LON_DEG] = true;
        out.vars[VAR_GPS_LON_DEG] = g_readings.gps_lon_deg;
    }
    if (g_readings.gps_fix_valid) {
        out.vars_valid[VAR_GPS_FIX] = true;
        out.vars[VAR_GPS_FIX] = g_readings.gps_fix ? 1.0f : 0.0f;
    }

    // --- Environmental variables ---
    if (g_readings.pressure_valid) {
        out.vars_valid[VAR_PRESSURE_HPA] = true;
        out.vars[VAR_PRESSURE_HPA] = g_readings.pressure_hpa;
    }
    if (g_readings.temp_valid) {
        out.vars_valid[VAR_TEMP_C] = true;
        out.vars[VAR_TEMP_C] = g_readings.temp_c;
    }
    if (g_readings.humidity_valid) {
        out.vars_valid[VAR_HUMIDITY_PCT] = true;
        out.vars[VAR_HUMIDITY_PCT] = g_readings.humidity_pct;
    }

    // --- Flags used by cut logic gating ---
    out.launch_detected = g_state.launch_detected;
    out.gps_fix_present = (g_readings.gps_fix_valid && g_readings.gps_fix);

    // --- External “user message” cut inputs ---
    for (uint8_t i = 0; i < NUM_EXTERNAL_INPUTS; i++) {
        out.external_cut_active[i] = g_readings.ext[i].debounced_active;
    }

    // Iridium remote cut request is filled by iridium_link (later).
    out.iridium_remote_cut_request = false;
}
