// project_config.h
#pragma once
/**
 * @file project_config.h
 * @brief Firmware identity and compile-time product constants for SkyGuard Cutdown Pro.
 *
 * What belongs here:
 *  - Firmware version identifiers (compile-time constants)
 *  - Build metadata (compile date/time)
 *  - Product-wide constants that are not user settings
 *
 * What does NOT belong here:
 *  - User-modifiable configuration (settings.h/.cpp and NVS)
 *  - Runtime state
 */

#include <Arduino.h>
#include <stdint.h>

const uint32_t GPS_BAUD = 115200;
static constexpr bool DEBUG_SERIAL = true;   // set false for production
static constexpr uint32_t DEBUG_SERIAL_BAUD = 115200;
/// How long config button must be held at boot to reset defaults (ms).
static constexpr uint32_t HOLD_AT_BOOT_DEFAULTS_MS = 3000;

/// Set true to print 1 Hz heartbeat + state to Serial.
static constexpr bool SERIAL_DEBUG = true;

/// Serial baud rate.
static constexpr uint32_t SERIAL_BAUD = 115200;
// -------------------------
// Firmware identity
// -------------------------

/**
 * @brief Firmware numeric version components.
 *
 * Single source of truth for firmware versioning.
 * Example: 0.1.0 => {0,1,0}
 */
static constexpr uint8_t FW_VERSION_MAJOR = 0;
static constexpr uint8_t FW_VERSION_MINOR = 1;
static constexpr uint8_t FW_VERSION_PATCH = 0;

/**
 * @brief Firmware build date/time, provided by the compiler.
 *
 * Notes:
 *  - __DATE__ format is compiler-dependent but typically like: "Dec 28 2025"
 *  - __TIME__ is typically: "14:03:22"
 */
static constexpr const char* FW_BUILD_DATE = __DATE__;
static constexpr const char* FW_BUILD_TIME = __TIME__;

/**
 * @brief Format the firmware version string into a caller-provided buffer.
 *
 * Produces: "v<major>.<minor>.<patch>" (e.g., "v0.1.0")
 *
 * @param out     Destination buffer.
 * @param out_len Buffer length in bytes.
 */
static inline void projectFormatVersion(char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    snprintf(out, out_len, "v%u.%u.%u", (unsigned)FW_VERSION_MAJOR,
             (unsigned)FW_VERSION_MINOR, (unsigned)FW_VERSION_PATCH);
    out[out_len - 1] = '\0';
}

/**
 * @brief Format a full firmware ID string into a caller-provided buffer.
 *
 * Produces: "SGCP v<maj>.<min>.<patch> (<date> <time>)"
 * Example:  "SGCP v0.1.0 (Dec 28 2025 14:03:22)"
 *
 * @param out     Destination buffer.
 * @param out_len Buffer length in bytes.
 */
static inline void projectFormatFirmwareId(char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    snprintf(out, out_len, "SGCP v%u.%u.%u (%s %s)",
             (unsigned)FW_VERSION_MAJOR,
             (unsigned)FW_VERSION_MINOR,
             (unsigned)FW_VERSION_PATCH,
             FW_BUILD_DATE, FW_BUILD_TIME);
    out[out_len - 1] = '\0';
}

// -------------------------
// Product constants (compile-time)
// -------------------------

/// Main scheduler tick rate in Hz (deterministic heartbeat).
static constexpr uint8_t MAIN_TICK_HZ = 1;

/// Must match settings.h bucket sizes.
static constexpr uint8_t MAX_BUCKET_CONDITIONS = 10;

/// Must match settings.h external input count.
static constexpr uint8_t NUM_EXTERNAL_INPUTS = 2;

// -------------------------
// Status LED patterns
// -------------------------

/// Status LED pulse width (ms). Short burst to save power.
static constexpr uint16_t STATUS_LED_PULSE_WIDTH_MS = 35;

/// Time between pulse starts within the 1-second frame (ms).
/// Must be large enough that 3 pulses fit inside 1000 ms.
static constexpr uint16_t STATUS_LED_PULSE_PERIOD_MS = 150;

/// NeoPixel brightness (0-255).
static constexpr uint8_t STATUS_LED_BRIGHTNESS = 32;

// Optional: if you coded blink counts as literals, define them too:
static constexpr uint8_t STATUS_LED_PULSES_GREEN = 1;
static constexpr uint8_t STATUS_LED_PULSES_YELLOW = 2;
static constexpr uint8_t STATUS_LED_PULSES_RED = 3;


// -------------------------
// GPS freshness thresholds
// -------------------------

/// Max acceptable age for GPS fields from TinyGPS++ (ms) to be treated as "fresh".
static constexpr uint32_t GPS_MAX_FIELD_AGE_MS = 3000;


// -------------------------
// Launch detection (v1)
// -------------------------

/// Launch detect threshold: GPS altitude rise above baseline (meters).
static constexpr float LAUNCH_GPS_ALT_RISE_M = 30.0f;

/// Launch detect threshold: barometric pressure drop below baseline (hPa).
static constexpr float LAUNCH_PRESSURE_DROP_HPA = 5.0f;

/// Launch detect persistence requirement: number of consecutive 1 Hz ticks.
static constexpr uint8_t LAUNCH_PERSIST_REQUIRED_S = 5;

// -------------------------
// SD logging
// -------------------------

static constexpr uint32_t SD_SPI_CLOCK_HZ = 4000000;   // start conservative; can raise later
static constexpr bool SD_CD_ACTIVE_LOW = true;

static constexpr uint16_t SD_LOG_LINE_MAX = 256;
static constexpr uint16_t SD_LOG_QUEUE_LINES = 240;
static constexpr const char* SD_LOG_DIR = "/";         // root
static constexpr const char* SD_LOG_EXT = ".TXT";

// If you want to log every 1 Hz tick:
static constexpr bool SD_LOG_EVERY_TICK = true;

// -------------------------
// Iridium link
// -------------------------
static constexpr uint32_t IRIDIUM_SERIAL_BAUD = 19200;     // RockBLOCK default is often 19200
static constexpr uint8_t  IRIDIUM_FAILS_BEFORE_ERROR = 3;

// If PIN_SAT_POWER enables power to modem:
static constexpr bool SAT_POWER_ACTIVE_HIGH = true;

// If you want to avoid long blocks, you can tune these later.
// (IridiumSBD calls can take seconds; that's acceptable at minute-scale cadence.)
