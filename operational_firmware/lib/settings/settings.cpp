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

/**
 * @brief Force a char buffer to be null-terminated.
 *
 * This prevents unterminated strings from causing read-overrun bugs in debug prints,
 * web UI, or protocol encoders.
 *
 * @param buf Pointer to character buffer.
 * @param len Size of the buffer in bytes.
 */
static void ensureNullTerminated(char* buf, size_t len) {
    if (!buf || len == 0) return;
    buf[len - 1] = '\0';
}

/**
 * @brief Returns true if a float is finite (not NaN/Inf).
 *
 * @param x Value to test.
 */
static bool isFiniteFloat(float x) {
    return isfinite(x);
}

/**
 * @brief Zero runtime-only fields that should never persist across boots.
 *
 * Currently: Condition::true_duration_s for all bucket conditions.
 *
 * @param cfg Settings to sanitize.
 */
static void zeroRuntimeFields(SystemConfig& cfg) {
    for (auto& c : cfg.bucketA) c.true_duration_s = 0.0f;
    for (auto& c : cfg.bucketB) c.true_duration_s = 0.0f;
}

/**
 * @brief Ensure all stored strings are safely null-terminated.
 *
 * @param cfg Settings to sanitize.
 */
static void sanitizeStrings(SystemConfig& cfg) {
    ensureNullTerminated(cfg.iridium.cutdown_token, sizeof(cfg.iridium.cutdown_token));
    ensureNullTerminated(cfg.fieldwatch.device_id, sizeof(cfg.fieldwatch.device_id));
    ensureNullTerminated(cfg.fieldwatch.access_token, sizeof(cfg.fieldwatch.access_token));
    ensureNullTerminated(cfg.device.ap_password, sizeof(cfg.device.ap_password));
}

/**
 * @brief Basic sanity validation for a loaded settings blob.
 *
 * Philosophy:
 *  - Reject obvious corruption (wrong magic/version, NaNs, out-of-range enums).
 *  - Avoid being overly strict on intervals (users may set odd values deliberately).
 *
 * @param blob Candidate blob read from NVS.
 * @return true if blob looks valid enough to use; false to force defaults.
 */
static bool validateBlob(const SettingsStorageBlob& blob) {
    if (blob.magic != SETTINGS_MAGIC) return false;
    if (blob.version != SETTINGS_VERSION) return false;

    // reserved should be zero when written; if not, treat as suspicious.
    // (This catches some kinds of partial/corrupt writes.)
    if (blob.reserved != 0) return false;

    const SystemConfig& cfg = blob.config;

    // Device: serial number must fit 7 digits (0 allowed as "unassigned").
    if (cfg.device.serial_number > 9999999u) return false;

    // Buckets: validate conditions in both buckets.
    auto validateCondition = [](const Condition& c) -> bool {
        if (c.var_id >= VAR__COUNT) return false;
        if (c.op > OP_GT) return false;

        // value must be sane if enabled; if disabled, we still prefer it to be finite.
        if (!isFiniteFloat(c.value)) return false;
        if (!isFiniteFloat(c.true_duration_s)) return false;

        // Dwell time is stored as uint16_t so it is inherently bounded.
        return true;
    };

    for (const auto& c : cfg.bucketA) {
        if (!validateCondition(c)) return false;
    }
    for (const auto& c : cfg.bucketB) {
        if (!validateCondition(c)) return false;
    }

    // External inputs: no strict validation needed beyond debounce sanity.
    for (const auto& in : cfg.external_inputs) {
        // debounce_ms of 0 is technically allowed, but it's usually a mistake.
        // We don't fail validation for it; just accept it.
        (void)in;
    }

    // Iridium scheduling: accept 0 as "disabled behavior", but reject absurd values that
    // are likely corruption. Keep validation intentionally permissive.
    auto saneInterval = [](uint32_t s) -> bool {
        // allow 0 (disabled), otherwise require >= 10 seconds and <= 7 days
        if (s == 0) return true;
        if (s < 10) return false;
        if (s > 7UL * 24UL * 3600UL) return false;
        return true;
    };

    if (!saneInterval(cfg.iridium.ground_interval_s)) return false;
    if (!saneInterval(cfg.iridium.ascent_interval_s)) return false;
    if (!saneInterval(cfg.iridium.descent_interval_s)) return false;
    if (!saneInterval(cfg.iridium.beacon_interval_s)) return false;
    if (!saneInterval(cfg.iridium.mailbox_check_interval_s)) return false;

    // Descent duration: allow 0, otherwise keep it within a sensible range.
    // (0 means "go straight to beacon behavior" if your iridium_link implements it that way.)
    if (cfg.iridium.descent_duration_s != 0) {
        if (cfg.iridium.descent_duration_s < 10) return false;
        if (cfg.iridium.descent_duration_s > 7UL * 24UL * 3600UL) return false;
    }

    // FieldWatch: strings are validated/sanitized for termination later.
    (void)cfg.fieldwatch;

    // AP password: we cannot safely strlen() an untrusted buffer before ensuring termination.
    // We'll enforce WPA2 minimum length after sanitizeStrings() runs post-load. Here we just accept.

    return true;
}

/**
 * @brief Read settings blob bytes from NVS.
 *
 * @param out_blob Destination blob.
 * @return true if bytes were read and size matches; false otherwise.
 */
static bool nvsReadBlob(SettingsStorageBlob& out_blob) {
    Preferences prefs;

    // Read-only open; if namespace doesn't exist, begin() still succeeds but key may not.
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

/**
 * @brief Write settings blob bytes to NVS.
 *
 * @param blob Blob to write.
 * @return true if write succeeded; false otherwise.
 */
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
    /**
     * Load settings, and if anything fails, apply defaults and persist.
     *
     * This function MUST leave g_settings valid.
     */
    if (settingsLoad()) {
        return;
    }

    settingsApplyDefaults(g_settings);
    zeroRuntimeFields(g_settings);
    sanitizeStrings(g_settings);

    // Best effort save; failure is non-fatal because defaults are already in RAM.
    (void)settingsSave();
}

bool settingsLoad() {
    /**
     * Read SettingsStorageBlob from NVS and populate g_settings.
     *
     * Returns false if:
     *  - key missing
     *  - wrong size
     *  - wrong magic/version
     *  - failed validation
     */
    SettingsStorageBlob blob;
    memset(&blob, 0, sizeof(blob));

    if (!nvsReadBlob(blob)) {
        return false;
    }

    if (!validateBlob(blob)) {
        return false;
    }

    // Copy into global config.
    g_settings = blob.config;

    // Always clear runtime fields and sanitize strings after load.
    zeroRuntimeFields(g_settings);
    sanitizeStrings(g_settings);

    // Enforce WPA2 minimum password length after sanitization (safe strlen).
    // If invalid, treat as corrupt and force defaults.
    if (strlen(g_settings.device.ap_password) < 8) {
        return false;
    }

    return true;
}

bool settingsSave() {
    /**
     * Persist g_settings as a SettingsStorageBlob.
     *
     * Writes reserved=0 and ensures strings are terminated before writing.
     */
    sanitizeStrings(g_settings);

    // Enforce WPA2 minimum password length.
    // Saving an invalid password would create an unusable AP in config mode.
    if (strlen(g_settings.device.ap_password) < 8) {
        return false;
    }

    SettingsStorageBlob blob;
    memset(&blob, 0, sizeof(blob));

    blob.magic = SETTINGS_MAGIC;
    blob.version = SETTINGS_VERSION;
    blob.reserved = 0;
    blob.config = g_settings;

    // Ensure runtime-only fields aren't accidentally persisted as "state".
    // (They are still physically in the struct; we just zero them before saving.)
    zeroRuntimeFields(blob.config);

    return nvsWriteBlob(blob);
}

void settingsApplyDefaults(SystemConfig& cfg) {
    /**
     * Conservative defaults:
     *  - No automatic cut rules enabled.
     *  - Launch required before rule-based cut.
     *  - External input 1 enabled.
     *  - Iridium enabled with remote cut allowed.
     *  - Token = "CUTDOWN"
     *  - FieldWatch disabled, IDs/tokens empty.
     *  - Device serial_number = 0 (unassigned), CONFIG AP password = "l33mange0"
     */
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
    // Input 0 (External input 1) enabled by default.
    cfg.external_inputs[0].enabled = true;
    cfg.external_inputs[0].active_high = true;
    cfg.external_inputs[0].debounce_ms = 50;

    // Input 1 (External input 2) disabled by default.
    cfg.external_inputs[1].enabled = false;
    cfg.external_inputs[1].active_high = true;
    cfg.external_inputs[1].debounce_ms = 50;

    // Iridium defaults
    // IMPORTANT: default disabled because some units may not have the modem installed.
    cfg.iridium.enabled = false;
    cfg.iridium.cutdown_on_command = true;
    memset(cfg.iridium.cutdown_token, 0, sizeof(cfg.iridium.cutdown_token));
    strncpy(cfg.iridium.cutdown_token, "CUTDOWN", sizeof(cfg.iridium.cutdown_token) - 1);

    // Telemetry cadence by phase (seconds)
    // 0 means "do not transmit in that phase"
    cfg.iridium.ground_interval_s = 0;         // usually don't spend money pre-launch
    cfg.iridium.ascent_interval_s = 300;       // 5 min in ascent/normal flight
    cfg.iridium.descent_interval_s = 120;      // 2 min after termination begins (recovery-critical)
    cfg.iridium.descent_duration_s = 3600;     // 60 min before switching to beacon
    cfg.iridium.beacon_interval_s = 1800;      // 30 min long-term beacon

    // Mailbox polling cadence (seconds) — only checked until cut or termination begins.
    cfg.iridium.mailbox_check_interval_s = 300; // 5 min


    // Reasonable starter intervals (seconds)
    cfg.iridium.ascent_interval_s = 300;       // 5 min pre-cut
    cfg.iridium.descent_interval_s = 300;      // 5 min shortly after cut
    cfg.iridium.descent_duration_s = 3600;     // 60 min descent window
    cfg.iridium.beacon_interval_s = 3600;      // 60 min long-term beacon


    // Reasonable starter intervals (seconds)
    cfg.iridium.ascent_interval_s = 300;       // 5 min pre-cut
    cfg.iridium.descent_interval_s = 300;      // 5 min shortly after cut
    cfg.iridium.descent_duration_s = 3600;     // 60 min descent window
    cfg.iridium.beacon_interval_s = 3600;      // 60 min long-term beacon

    // FieldWatch defaults
    cfg.fieldwatch.enabled = false;
    memset(cfg.fieldwatch.device_id, 0, sizeof(cfg.fieldwatch.device_id));
    memset(cfg.fieldwatch.access_token, 0, sizeof(cfg.fieldwatch.access_token));

    // Device defaults
    cfg.device.serial_number = 0;
    memset(cfg.device.ap_password, 0, sizeof(cfg.device.ap_password));
    strncpy(cfg.device.ap_password, "l33mange0", sizeof(cfg.device.ap_password) - 1);

    g_settings.term.enabled = true;
    g_settings.term.sustain_s = 15;

    g_settings.term.use_gps = true;
    g_settings.term.gps_drop_m = 60.0f;

    g_settings.term.use_pressure = true;
    g_settings.term.pressure_rise_hpa = 50.0f;

    // Final safety hygiene
    zeroRuntimeFields(cfg);
    sanitizeStrings(cfg);
}

bool settingsResetToDefaultsAndSave() {
    /**
     * Apply defaults to g_settings and persist immediately.
     *
     * Important: Do NOT modify the device serial number on reset.
     * This preserves unit identity across factory resets.
     */
    const uint32_t preserved_serial = g_settings.device.serial_number;

    settingsApplyDefaults(g_settings);

    // Restore serial after defaults.
    g_settings.device.serial_number = preserved_serial;

    return settingsSave();
}
