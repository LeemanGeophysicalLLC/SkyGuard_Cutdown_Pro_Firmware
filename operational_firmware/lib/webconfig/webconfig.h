// webconfig.h
#pragma once
/**
 * @file webconfig.h
 * @brief Configuration-mode entry (button), WiFi AP + web UI, and OTA updates.
 *
 * Philosophy:
 *  - Configuration mode is intentionally simple and blocking (FieldTemp pattern).
 *  - Entering config mode pauses all other firmware behaviors.
 *  - Exiting config mode ALWAYS triggers ESP.restart() for a clean startup.
 *  - Auto-exit after a fixed timeout (5 minutes) if user does not save.
 *
 * Web UI goals:
 *  - Serve a static HTML page (your draft) with values populated server-side.
 *  - Use form POST for /save.
 *  - Provide useful validation errors and field-specific feedback on the page.
 *  - Provide OTA firmware update endpoint just like FieldTemp.
 *
 * Security:
 *  - CONFIG AP uses WPA2 password from settings: g_settings.device.ap_password
 *  - SSID is "CONFIG-<serial>" where serial is g_settings.device.serial_number (0 allowed).
 */

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Webconfig runtime options.
 *
 * Keep these compile-time constant unless you have a strong reason otherwise.
 */
struct WebConfigOptions {
    uint32_t config_timeout_ms;   ///< Time from entry until auto-exit/restart if not saved.
    uint16_t http_port;           ///< HTTP port for the config server (usually 80).
    bool     enable_ota;           ///< Enable OTA update routes.
};

/**
 * @brief Result of validating a submitted configuration form.
 *
 * This is used to generate user-visible error feedback on the HTML page.
 * The implementation may choose to show a summary plus per-field markers.
 */
struct WebConfigValidationResult {
    bool ok;                       ///< True if the candidate config is valid and can be saved.
    uint16_t error_count;          ///< Number of validation errors.

    // A short summary suitable for a banner on the page.
    // Example: "3 problems: WiFi password too short; Bucket A row 2 invalid; Iridium interval out of range"
    char summary[256];
};

/**
 * @brief Initialize the webconfig subsystem.
 *
 * Call once during boot (e.g., in setup()).
 *
 * Responsibilities:
 *  - Configure the config button GPIO (input + pullups as appropriate).
 *  - Load default options.
 *
 * Notes:
 *  - This does NOT start WiFi or the web server. That happens only in config mode.
 */
void webconfigInit();

/**
 * @brief Poll the configuration button during normal operation.
 *
 * Call frequently (fast path) from loop().
 *
 * Behavior:
 *  - If button press is detected (debounced), enters config mode immediately
 *    (blocking) and will restart on exit.
 *
 * @return true if the call resulted in entering config mode (function will not return
 *         until config mode exits); false if nothing happened.
 */
bool webconfigPollButton();

/**
 * @brief Check for "hold at boot" condition and factory reset if requested.
 *
 * Call early in setup(), before entering normal flight logic.
 *
 * Behavior:
 *  - If config button is held for the required duration, reset settings to defaults
 *    BUT DO NOT TOUCH serial number, then restart.
 *
 * @param hold_ms Duration button must be held to trigger reset.
 * @return true if a reset/restart was triggered (function may not return); false otherwise.
 */
bool webconfigCheckHoldAtBoot(uint32_t hold_ms);

/**
 * @brief Enter configuration mode.
 *
 * This is a blocking call (FieldTemp pattern).
 *
 * Behavior:
 *  - Starts WiFi AP (SSID "CONFIG-<serial>", WPA2 password from settings).
 *  - Starts HTTP server and registers routes.
 *  - Serves the settings page and handles POST actions.
 *  - If user clicks Save and validation passes: saves settings and restarts.
 *  - If user clicks Exit: restarts.
 *  - If timeout expires without a successful Save: restarts.
 *
 * Notes:
 *  - This function should be called only when the main firmware is ready to "pause everything".
 */
void webconfigEnter();

/**
 * @brief Get the default webconfig options.
 *
 * @return Options with sane defaults (timeout=5 min, port=80, OTA enabled).
 */
WebConfigOptions webconfigGetDefaultOptions();

/**
 * @brief Set webconfig options (optional).
 *
 * If you never call this, defaults are used.
 *
 * @param opts Options to apply.
 */
void webconfigSetOptions(const WebConfigOptions& opts);

/**
 * @brief Build the CONFIG SSID string into a caller-provided buffer.
 *
 * Produces: "CONFIG-<serial>" (e.g., "CONFIG-1234567").
 * If serial is 0, still produces "CONFIG-0".
 *
 * @param out Buffer to fill.
 * @param out_len Buffer length in bytes.
 */
void webconfigFormatSsid(char* out, size_t out_len);

/**
 * @brief Validate a candidate configuration parsed from a web form POST.
 *
 * Implementation notes:
 *  - Should validate ranges and required fields.
 *  - Should populate a human-readable summary.
 *  - Should not modify g_settings directly.
 *
 * @param candidate Candidate config (typically built from current g_settings then overwritten with form fields).
 * @param out_result Validation result with summary.
 */
void webconfigValidateCandidate(const struct SystemConfig& candidate, WebConfigValidationResult& out_result);
