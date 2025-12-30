// settings.h
#pragma once

/**
 * @file settings.h
 * @brief Persistent user configuration for SkyGuard Cutdown Pro.
 *
 * Design goals:
 *  - All user-configurable behavior lives here (rule engine, cut toggles, Iridium, external inputs, FieldWatch, device/AP config).
 *  - Stored as a single binary blob in ESP32 NVS (no JSON).
 *  - Versioned with a magic number for corruption detection and safe migration later.
 *  - Safe defaults on first boot or invalid data.
 *
 * Invariants:
 *  - Exactly one global settings instance exists: g_settings.
 *  - Runtime state (timers, latches, one-shot flags) does NOT belong here.
 */

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include "project_config.h"

/// Current settings blob version. Increment when SettingsStorageBlob layout changes.
static constexpr uint16_t SETTINGS_VERSION = 1;

/// 32-bit magic tag used to verify NVS data is ours ("SGCP").
static constexpr uint32_t SETTINGS_MAGIC = 0x53474350u; // 'S''G''C''P'

/**
 * @enum VariableId
 * @brief Identifiers for the rule engine variables that conditions can test.
 *
 * Notes:
 *  - All variables are treated as numeric. "Boolean" values are represented as 0 or 1.
 *  - The mapping from VariableId -> live value is implemented in cut_logic/sensors, not here.
 */
enum VariableId : uint8_t {
    VAR_T_POWER_S = 0,     ///< Seconds since boot/power-on (uptime).
    VAR_T_LAUNCH_S,        ///< Seconds since launch was detected (0 if not launched).
    VAR_GPS_ALT_M,         ///< GPS altitude in meters.
    VAR_GPS_LAT_DEG,       ///< Latitude in degrees.
    VAR_GPS_LON_DEG,       ///< Longitude in degrees.
    VAR_GPS_FIX,           ///< GPS fix present (0/1).
    VAR_PRESSURE_HPA,      ///< Ambient pressure in hPa.
    VAR_TEMP_C,            ///< Temperature in °C.
    VAR_HUMIDITY_PCT,      ///< Relative humidity in %.
    VAR__COUNT             ///< Count sentinel (not a real variable).
};

/**
 * @enum CompareOp
 * @brief Comparison operators available for rule conditions.
 *
 * Stored as a compact uint8_t in NVS.
 */
enum CompareOp : uint8_t {
    OP_LT = 0,   ///< <
    OP_LTE,      ///< <=
    OP_EQ,       ///< ==
    OP_GTE,      ///< >=
    OP_GT        ///< >
};

/**
 * @struct Condition
 * @brief One rule condition evaluated against a single variable.
 *
 * Semantics:
 *  - If enabled=false, this condition is ignored.
 *  - The comparison uses (variable_value OP value).
 *  - for_seconds specifies dwell time: how long the condition must remain true
 *    continuously before the condition is considered satisfied.
 *  - for_seconds=0 means "immediate" (true on the first tick it evaluates true).
 *
 * Important:
 *  - true_duration_s is RUNTIME STATE and should not be persisted in NVS long-term.
 *  - However, to keep the stored settings blob simple and flat, we keep it here as a field
 *    and the firmware will zero it on load/startup.
 *
 *  You may later choose to move runtime accumulators into state/cut_logic if desired.
 */
struct Condition {
    bool     enabled;           ///< If false, condition is ignored.
    uint8_t  var_id;            ///< VariableId (stored as uint8_t for compactness).
    uint8_t  op;                ///< CompareOp (stored as uint8_t).
    float    value;             ///< Threshold/compare value.
    uint16_t for_seconds;       ///< Required continuous-true duration (0 = immediate).

    // --- Runtime field (not intended to persist across power cycles) ---
    float    true_duration_s;   ///< Accumulator (seconds true). Reset on boot/load.
};

/**
 * @struct GlobalCutdownConfig
 * @brief Global gating requirements applied before rule-based cut can fire.
 *
 * These do NOT affect immediate cuts (external inputs and Iridium remote cut).
 */
struct GlobalCutdownConfig {
    bool require_launch_before_cut;   ///< If true, rule-based cut is blocked until launch_detected.
    bool require_gps_fix_before_cut;  ///< If true, rule-based cut is blocked until GPS fix is present.
};

/**
 * @struct ExternalInputConfig
 * @brief Configuration for one optoisolated external cut input.
 *
 * Behavior:
 *  - If enabled and the input is detected active (after debounce), firmware triggers immediate cut.
 *  - This bypasses bucket logic and global cut toggles.
 */
struct ExternalInputConfig {
    bool     enabled;        ///< Enable/disable this input.
    bool     active_high;    ///< If true, active when pin reads HIGH; otherwise active when LOW.
    uint16_t debounce_ms;    ///< Debounce interval in milliseconds.
};

struct IridiumConfig {
    bool     enabled;                 ///< Master enable for Iridium subsystem (units may omit modem).
    bool     cutdown_on_command;       ///< Allow remote cut command.
    char     cutdown_token[16];        ///< ASCII token required to authorize remote cut (null-terminated).

    // Telemetry cadence by phase (seconds). 0 means "do not transmit in that phase".
    uint32_t ground_interval_s;        ///< Pre-launch.
    uint32_t ascent_interval_s;        ///< Ascending / normal flight.
    uint32_t descent_interval_s;       ///< After termination begins (cut or pop) for an initial period.
    uint32_t beacon_interval_s;        ///< Long-tail beacon after descent window.

    // Phase transition: how long after termination we remain in "descent" before switching to "beacon".
    uint32_t descent_duration_s;       ///< Seconds after termination start to use descent_interval_s.
    
    // Mailbox polling cadence (seconds). Only checked until cut OR termination start.
    uint32_t mailbox_check_interval_s; ///< Interval to check the Iridium mailbox.
};


/**
 * @struct FieldWatchConfig
 * @brief Credentials and routing identifiers for FieldWatch telemetry ingestion.
 *
 * Typical examples:
 *  - device_id:    "7bd3cd90-b03c-11f0-b8ab-b38af83f6c9c" (UUID string)
 *  - access_token: "qngd8wfonx5zjozknt2g"
 *
 * Notes:
 *  - These are user/configuration values (not firmware identity), so they belong in settings.
 *  - Strings are fixed-size buffers for NVS stability and simplicity.
 *  - Implementations should ensure these are always null-terminated.
 */
struct FieldWatchConfig {
    bool enabled;                 ///< Master enable for FieldWatch routing.
    char device_id[48];           ///< Device identifier (UUID-like). Null-terminated ASCII.
    char access_token[64];        ///< Access token / API token. Null-terminated ASCII.
};


typedef struct {
    bool     enabled;                 // master enable
    uint16_t sustain_s;               // seconds condition must hold (1 Hz ticks)

    float    gps_drop_m;              // e.g. 60.0
    float    pressure_rise_hpa;        // e.g. 50.0

    bool     use_gps;                 // allow GPS-based detection
    bool     use_pressure;            // allow pressure-based detection
} TerminationDetectConfig;

/**
 * @struct DeviceConfig
 * @brief Device identity and CONFIG-mode WiFi credentials.
 *
 * Requirements:
 *  - serial_number is a positive integer identifier (up to 7 digits).
 *    0 means "unassigned / not set yet".
 *  - ap_password is the WPA2 password for CONFIG-mode AP.
 *    Default is "l33mange0". Must be >= 8 characters for WPA2.
 *
 * Reset behavior:
 *  - Factory reset to defaults MUST NOT modify serial_number.
 */
struct DeviceConfig {
    uint32_t serial_number;   ///< 0..9,999,999 (7 digits). 0 means unassigned.
    char     ap_password[32]; ///< CONFIG AP WPA2 password (null-terminated ASCII).
};

/**
 * @struct SystemConfig
 * @brief The full user configuration stored in NVS (inside SettingsStorageBlob).
 *
 * Buckets:
 *  - Bucket A: ALL enabled must be true. If Bucket A has no enabled conditions -> treated as TRUE.
 *  - Bucket B: ANY enabled must be true. If Bucket B has no enabled conditions -> treated as FALSE.
 */
struct SystemConfig {
    GlobalCutdownConfig  global_cutdown;                       ///< Global cut gating.
    Condition            bucketA[MAX_BUCKET_CONDITIONS];       ///< AND bucket of settings.
    Condition            bucketB[MAX_BUCKET_CONDITIONS];       ///< OR bucket of settings.
    ExternalInputConfig  external_inputs[NUM_EXTERNAL_INPUTS]; ///< Optoisolated inputs.
    IridiumConfig        iridium;                              ///< Iridium behavior.
    FieldWatchConfig     fieldwatch;                           ///< FieldWatch routing credentials/IDs.
    DeviceConfig         device;                               ///< Device serial + CONFIG AP credentials.
    TerminationDetectConfig term;
};

/**
 * @struct SettingsStorageBlob
 * @brief The binary blob stored in ESP32 NVS.
 *
 * Load procedure should verify:
 *  - magic == SETTINGS_MAGIC
 *  - version == SETTINGS_VERSION (or handle migration)
 *  - basic sanity checks (optional, but recommended)
 */
struct SettingsStorageBlob {
    uint32_t     magic;     ///< SETTINGS_MAGIC ("SGCP")
    uint16_t     version;   ///< SETTINGS_VERSION
    uint16_t     reserved;  ///< Reserved for alignment/future use (must be zero when saving)
    SystemConfig config;    ///< The user configuration.
};

/// Global singleton settings instance used by all firmware modules.
extern SystemConfig g_settings;

void settingsInit();
bool settingsLoad();
bool settingsSave();
void settingsApplyDefaults(SystemConfig& cfg);
bool settingsResetToDefaultsAndSave();
