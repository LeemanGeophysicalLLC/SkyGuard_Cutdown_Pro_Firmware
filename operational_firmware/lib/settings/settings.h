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

enum VariableId : uint8_t {
    VAR_T_POWER_S = 0,
    VAR_T_LAUNCH_S,
    VAR_GPS_ALT_M,
    VAR_GPS_LAT_DEG,
    VAR_GPS_LON_DEG,
    VAR_GPS_FIX,
    VAR_PRESSURE_HPA,
    VAR_TEMP_C,
    VAR_HUMIDITY_PCT,
    VAR__COUNT
};

enum CompareOp : uint8_t {
    OP_LT = 0,
    OP_LTE,
    OP_EQ,
    OP_GTE,
    OP_GT
};

struct Condition {
    bool     enabled;
    uint8_t  var_id;
    uint8_t  op;
    float    value;
    uint16_t for_seconds;

    // --- Runtime field (not intended to persist across power cycles) ---
    float    true_duration_s;
};

struct GlobalCutdownConfig {
    bool require_launch_before_cut;
    bool require_gps_fix_before_cut;
};

struct ExternalInputConfig {
    bool     enabled;
    bool     active_high;
    uint16_t debounce_ms;
};

struct IridiumConfig {
    bool     enabled;
    bool     cutdown_on_command;
    char     cutdown_token[16];

    // Telemetry cadence by phase (seconds). 0 means "do not transmit in that phase".
    uint32_t ground_interval_s;
    uint32_t ascent_interval_s;
    uint32_t descent_interval_s;
    uint32_t beacon_interval_s;

    // Phase transition: how long after termination we remain in "descent" before switching to "beacon".
    uint32_t descent_duration_s;
};

struct FieldWatchConfig {
    bool enabled;
    char device_id[48];
    char access_token[64];
};

struct TerminationDetectConfig {
    bool     enabled;            ///< master enable
    uint16_t sustain_s;          ///< seconds condition must hold (1 Hz ticks)

    bool     use_gps;            ///< allow GPS-based detection
    float    gps_drop_m;         ///< peak-drop threshold in meters

    bool     use_pressure;       ///< allow pressure-based detection
    float    pressure_rise_hpa;  ///< min-rise threshold in hPa
};

struct DeviceConfig {
    uint32_t serial_number;
    char     ap_password[32];
};

struct SystemConfig {
    GlobalCutdownConfig   global_cutdown;
    Condition             bucketA[MAX_BUCKET_CONDITIONS];
    Condition             bucketB[MAX_BUCKET_CONDITIONS];
    ExternalInputConfig   external_inputs[NUM_EXTERNAL_INPUTS];
    IridiumConfig         iridium;
    FieldWatchConfig      fieldwatch;
    DeviceConfig          device;

    // Balloon-pop / descent termination detector configuration
    TerminationDetectConfig term;
};

struct SettingsStorageBlob {
    uint32_t     magic;
    uint16_t     version;
    uint16_t     reserved;
    SystemConfig config;
};

/// Global singleton settings instance used by all firmware modules.
extern SystemConfig g_settings;

void settingsInit();
bool settingsLoad();
bool settingsSave();
void settingsApplyDefaults(SystemConfig& cfg);
bool settingsResetToDefaultsAndSave();
