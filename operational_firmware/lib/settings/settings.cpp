// settings.cpp
/**
 * @file settings.cpp
 * @brief NVS-backed persistence for SkyGuard Cutdown Pro configuration.
 *
 * Uses ESP32 Preferences (NVS) to store a single binary blob (SettingsStorageBlob).
 * This file is intentionally conservative: if anything looks wrong, we fall back
 * to safe defaults and (optionally) re-save them.
 */

#include "settings.h"

#include <Preferences.h>
#include <string.h>
#include <math.h>

// -------------------------
// NVS storage identifiers
// -------------------------

/// NVS namespace for all settings-related keys.
static constexpr const char* NVS_NAMESPACE = "sgcp";

/// NVS key for the settings blob.
static constexpr const char* NVS_KEY_SETTINGS_BLOB = "settings";

// -------------------------
// Global settings instance
// -------------------------

SystemConfig g_settings;

// -------------------------
// Internal helpers
// -------------------------

static void ensureNullTerminated(char* buf, size_t len) {
    if (!buf || len == 0) return;
    buf[len - 1] = '\0';
}

static bool isFiniteFloat(float x) {
    return isfinite(x);
}

static void zeroRuntimeFields(SystemConfig& cfg) {
    for (auto& c : cfg.bucketA) c.true_duration_s = 0.0f;
    for (auto& c : cfg.bucketB) c.true_duration_s = 0.0f;
}

static void sanitizeStrings(SystemConfig& cfg) {
    ensureNullTerminated(cfg.iridium.cutdown_token, sizeof(cfg.iridium.cutdown_token));
    ensureNullTerminated(cfg.fieldwatch.device_id, sizeof(cfg.fieldwatch.device_id));
    ensureNullTerminated(cfg.fieldwatch.access_token, sizeof(cfg.fieldwatch.access_token));
    ensureNullTerminated(cfg.device.ap_password, sizeof(cfg.device.ap_password));
}

/**
 * @brief Basic sanity validation for a loaded settings blob.
 */
static bool validateBlob(const SettingsStorageBlob& blob) {
    if (blob.magic != SETTINGS_MAGIC) return false;
    if (blob.version != SETTINGS_VERSION) return false;

    if (blob.reserved != 0) return false;

    const SystemConfig& cfg = blob.config;

    if (cfg.device.serial_number > 9999999u) return false;

    auto validateCondition = [](const Condition& c) -> bool {
        if (c.var_id >= VAR__COUNT) return false;
        if (c.op > OP_GT) return false;
        if (!isFiniteFloat(c.value)) return false;
        if (!isFiniteFloat(c.true_duration_s)) return false;
        return true;
    };

    for (const auto& c : cfg.bucketA) if (!validateCondition(c)) return false;
    for (const auto& c : cfg.bucketB) if (!validateCondition(c)) return false;

    for (const auto& in : cfg.external_inputs) {
        (void)in;
    }

    auto saneInterval = [](uint32_t s) -> bool {
        if (s == 0) return true;                 // disabled is OK
        if (s < 10) return false;                // too small likely corruption
        if (s > 7UL * 24UL * 3600UL) return false; // > 7 days unlikely intentional
        return true;
    };

    if (!saneInterval(cfg.iridium.ground_interval_s)) return false;
    if (!saneInterval(cfg.iridium.ascent_interval_s)) return false;
    if (!saneInterval(cfg.iridium.descent_interval_s)) return false;
    if (!saneInterval(cfg.iridium.beacon_interval_s)) return false;

    if (cfg.iridium.descent_duration_s != 0) {
        if (cfg.iridium.descent_duration_s < 10) return false;
        if (cfg.iridium.descent_duration_s > 7UL * 24UL * 3600UL) return false;
    }

    (void)cfg.fieldwatch;

    return true;
}

static bool nvsReadBlob(SettingsStorageBlob& out_blob) {
    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, true)) {
        return false;
    }

    const size_t expected = sizeof(SettingsStorageBlob);
    const size_t got = prefs.getBytesLength(NVS_KEY_SETTINGS_BLOB);

    if (got != expected) {
        prefs.end();
        return false;
    }

    const size_t read = prefs.getBytes(NVS_KEY_SETTINGS_BLOB, &out_blob, expected);
    prefs.end();

    return (read == expected);
}

static bool nvsWriteBlob(const SettingsStorageBlob& blob) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    const size_t written = prefs.putBytes(NVS_KEY_SETTINGS_BLOB, &blob, sizeof(SettingsStorageBlob));
    prefs.end();

    return (written == sizeof(SettingsStorageBlob));
}

// -------------------------
// Public API implementation
// -------------------------

void settingsInit() {
    if (settingsLoad()) {
        return;
    }

    settingsApplyDefaults(g_settings);
    zeroRuntimeFields(g_settings);
    sanitizeStrings(g_settings);

    (void)settingsSave();
}

bool settingsLoad() {
    SettingsStorageBlob blob;
    memset(&blob, 0, sizeof(blob));

    if (!nvsReadBlob(blob)) {
        return false;
    }

    if (!validateBlob(blob)) {
        return false;
    }

    g_settings = blob.config;

    zeroRuntimeFields(g_settings);
    sanitizeStrings(g_settings);

    if (strlen(g_settings.device.ap_password) < 8) {
        return false;
    }

    return true;
}

bool settingsSave() {
    sanitizeStrings(g_settings);

    if (strlen(g_settings.device.ap_password) < 8) {
        return false;
    }

    SettingsStorageBlob blob;
    memset(&blob, 0, sizeof(blob));

    blob.magic = SETTINGS_MAGIC;
    blob.version = SETTINGS_VERSION;
    blob.reserved = 0;
    blob.config = g_settings;

    zeroRuntimeFields(blob.config);

    return nvsWriteBlob(blob);
}

void settingsApplyDefaults(SystemConfig& cfg) {
    memset(&cfg, 0, sizeof(SystemConfig));

    // Global cut gating
    cfg.global_cutdown.require_launch_before_cut = true;
    cfg.global_cutdown.require_gps_fix_before_cut = false;

    // Buckets: disabled by default
    auto initCondition = [](Condition& c) {
        c.enabled = false;
        c.var_id = VAR_T_POWER_S;
        c.op = OP_GT;
        c.value = 0.0f;
        c.for_seconds = 0;
        c.true_duration_s = 0.0f;
    };
    for (auto& c : cfg.bucketA) initCondition(c);
    for (auto& c : cfg.bucketB) initCondition(c);

    // External inputs
    cfg.external_inputs[0].enabled = true;
    cfg.external_inputs[0].active_high = true;
    cfg.external_inputs[0].debounce_ms = 50;

    cfg.external_inputs[1].enabled = false;
    cfg.external_inputs[1].active_high = true;
    cfg.external_inputs[1].debounce_ms = 50;

    // Iridium defaults (disabled by default; some units omit modem)
    cfg.iridium.enabled = false;
    cfg.iridium.cutdown_on_command = true;
    memset(cfg.iridium.cutdown_token, 0, sizeof(cfg.iridium.cutdown_token));
    strncpy(cfg.iridium.cutdown_token, "CUTDOWN", sizeof(cfg.iridium.cutdown_token) - 1);

    // Telemetry cadence by phase (seconds). 0 means "do not transmit in that phase".
    cfg.iridium.ground_interval_s  = 0;      // usually don't spend money pre-launch
    cfg.iridium.ascent_interval_s  = 300;    // 5 min in ascent/normal flight
    cfg.iridium.descent_interval_s = 120;    // 2 min after termination begins
    cfg.iridium.descent_duration_s = 3600;   // 60 min before switching to beacon
    cfg.iridium.beacon_interval_s  = 1800;   // 30 min beacon

    // FieldWatch defaults
    cfg.fieldwatch.enabled = false;
    memset(cfg.fieldwatch.device_id, 0, sizeof(cfg.fieldwatch.device_id));
    memset(cfg.fieldwatch.access_token, 0, sizeof(cfg.fieldwatch.access_token));

    // Device defaults
    cfg.device.serial_number = 0;
    memset(cfg.device.ap_password, 0, sizeof(cfg.device.ap_password));
    strncpy(cfg.device.ap_password, "l33mange0", sizeof(cfg.device.ap_password) - 1);

    // Termination detector defaults (dual-mode)
    cfg.term.enabled = true;
    cfg.term.sustain_s = 15;

    cfg.term.use_gps = true;
    cfg.term.gps_drop_m = 60.0f;

    cfg.term.use_pressure = true;
    cfg.term.pressure_rise_hpa = 50.0f;

    // Final safety hygiene
    zeroRuntimeFields(cfg);
    sanitizeStrings(cfg);
}

bool settingsResetToDefaultsAndSave() {
    const uint32_t preserved_serial = g_settings.device.serial_number;

    settingsApplyDefaults(g_settings);

    g_settings.device.serial_number = preserved_serial;

    return settingsSave();
}
