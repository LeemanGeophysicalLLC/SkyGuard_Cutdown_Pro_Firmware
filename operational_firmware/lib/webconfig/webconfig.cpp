// webconfig.cpp
/**
 * @file webconfig.cpp
 * @brief CONFIG-button-driven WiFi AP + Web UI configuration mode + OTA firmware updates.
 *
 * FieldTemp-style design:
 *  - During normal operation, poll the config button (fast path).
 *  - At boot, holding config button triggers factory reset (defaults) WITHOUT clearing serial number.
 *  - Entering config mode pauses all other logic.
 *  - Config mode is blocking: handleClient() loop until Save/Exit/Defaults or timeout.
 *  - Exiting config mode ALWAYS calls ESP.restart() to guarantee a clean startup.
 *
 * Web UI:
 *  - GET  /            -> settings page HTML
 *  - POST /save        -> parse form, validate, save, show feedback, restart
 *  - POST /exit        -> restart
 *  - POST /defaults    -> restore defaults (serial preserved by settings module), restart
 *  - POST /lock        -> (optional) lock release mechanism (stub until implemented)
 *  - POST /release     -> (optional) release mechanism (stub until implemented)
 *  - GET  /firmware    -> OTA upload page
 *  - POST /firmware    -> OTA upload handler (Update.h)
 *
 * Notes:
 *  - This file assumes your HTML uses the field names we agreed on:
 *    serial_number, ap_password,
 *    fw_enabled, fw_device_id, fw_access_token,
 *    gc_require_launch, gc_require_fix,
 *    ext0_enabled/ext0_active_high/ext0_debounce_ms, ext1_...,
 *    ir_enabled/ir_remote_cut/ir_token/ir_ascent_s/ir_descent_s/ir_descent_dur_s/ir_beacon_s,
 *    a0_*..a9_* and b0_*..b9_* for bucket conditions.
 *
 * Banner feedback:
 *  - We inject a small banner div immediately after the <body> tag at response time.
 *    This avoids permanently modifying your HTML file.
 */

#include "webconfig.h"

#include "pins.h"
#include "settings.h"
#include "readings.h"
#include "state.h"
#include "iridium_link.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

#include <string.h>
#include <math.h>

#include "page_settings.h"
#include "page_firmware.h"


// -------------------------
// Internal config / defaults
// -------------------------

/// Debounce interval for config button (ms).
static constexpr uint32_t CONFIG_BTN_DEBOUNCE_MS = 30;

/// Minimum press duration to count as an intentional "enter config mode" press (ms).
static constexpr uint32_t CONFIG_BTN_MIN_PRESS_MS = 80;

/// Default config-mode auto-exit timeout (ms) if user does not save.
static constexpr uint32_t CONFIG_MODE_TIMEOUT_MS = 5UL * 60UL * 1000UL; // 5 minutes

/// Optional: small delay between handleClient() calls.
static constexpr uint32_t SERVER_LOOP_DELAY_MS = 1;

/// Web server instance used only during config mode.
static WebServer* g_server = nullptr;

/// Config mode options currently in use (can be overridden by webconfigSetOptions()).
static WebConfigOptions g_webcfg_opts = {
    CONFIG_MODE_TIMEOUT_MS, // config_timeout_ms
    80,                     // http_port
    true                    // enable_ota
};

// -------------------------
// HTML content
// -------------------------



// -------------------------
// Internal helpers: button debounce
// -------------------------

/**
 * @brief Read the raw config button level.
 *
 * Button is pulled up and reads LOW when pressed.
 *
 * @return true if currently pressed; false otherwise.
 */
static bool configButtonPressedRaw() {
    return (digitalRead(PIN_CONFIG_BUTTON) == LOW);
}

/**
 * @brief Simple debounced button sampler.
 *
 * Tracks a stable level for debounce_ms before accepting a change.
 */
struct DebouncedButton {
    bool     stable_pressed = false;   ///< Debounced pressed state
    bool     last_raw = false;         ///< Last raw read
    uint32_t last_change_ms = 0;       ///< When raw state last changed
};

/**
 * @brief Update debounced state and report rising/falling edges.
 */
static void updateDebouncedButton(DebouncedButton& btn,
                                  uint32_t now_ms,
                                  uint32_t debounce_ms,
                                  bool& out_pressed_edge,
                                  bool& out_released_edge) {
    out_pressed_edge = false;
    out_released_edge = false;

    const bool raw = configButtonPressedRaw();

    if (raw != btn.last_raw) {
        btn.last_raw = raw;
        btn.last_change_ms = now_ms;
    }

    if ((now_ms - btn.last_change_ms) >= debounce_ms) {
        if (btn.stable_pressed != btn.last_raw) {
            const bool prev = btn.stable_pressed;
            btn.stable_pressed = btn.last_raw;

            if (!prev && btn.stable_pressed) out_pressed_edge = true;
            if (prev && !btn.stable_pressed) out_released_edge = true;
        }
    }
}

static DebouncedButton g_cfg_btn;
static uint32_t g_press_start_ms = 0;

// -------------------------
// Internal helpers: strings & parsing
// -------------------------

/**
 * @brief Ensure a buffer is null-terminated.
 */
static void ensureNullTerminated(char* buf, size_t len) {
    if (!buf || len == 0) return;
    buf[len - 1] = '\0';
}

/**
 * @brief Copy a server arg into a fixed char buffer safely.
 */
static void copyArgToBuf(char* dst, size_t dst_len, const String& src) {
    if (!dst || dst_len == 0) return;
    memset(dst, 0, dst_len);
    const size_t n = (src.length() < (int)(dst_len - 1)) ? (size_t)src.length() : (dst_len - 1);
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    dst[dst_len - 1] = '\0';
}

/**
 * @brief Convert a String to uint32 with fallback.
 */
static uint32_t toU32(const String& s, uint32_t def) {
    if (s.length() == 0) return def;
    return (uint32_t)strtoul(s.c_str(), nullptr, 10);
}

/**
 * @brief Convert a String to uint16 with fallback.
 */
static uint16_t toU16(const String& s, uint16_t def) {
    if (s.length() == 0) return def;
    const unsigned long v = strtoul(s.c_str(), nullptr, 10);
    return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v;
}

/**
 * @brief Convert a String to float with fallback.
 */
static float toF32(const String& s, float def) {
    if (s.length() == 0) return def;
    return strtof(s.c_str(), nullptr);
}

/**
 * @brief Convert a String to bool with fallback.
 *
 * Accepts: "1", "true", "on" => true; "0", "false", "off" => false.
 */
static bool toBool(const String& s, bool def) {
    if (s.length() == 0) return def;
    if (s == "1" || s == "true" || s == "on" || s == "ON") return true;
    if (s == "0" || s == "false" || s == "off" || s == "OFF") return false;
    return def;
}

/**
 * @brief Parse a variable token from HTML into VariableId.
 *
 * HTML values:
 *  t_power_s, t_launch_s, gps_alt_m, gps_lat_deg, gps_lon_deg, gps_fix,
 *  pressure_hPa, temp_C, humidity_pct
 *
 * @param token HTML token
 * @param out_id Filled on success
 * @return true on success; false on unknown token.
 */
static bool parseVarId(const String& token, uint8_t& out_id) {
    if (token == "t_power_s")   { out_id = VAR_T_POWER_S; return true; }
    if (token == "t_launch_s")  { out_id = VAR_T_LAUNCH_S; return true; }
    if (token == "gps_alt_m")   { out_id = VAR_GPS_ALT_M; return true; }
    if (token == "gps_lat_deg") { out_id = VAR_GPS_LAT_DEG; return true; }
    if (token == "gps_lon_deg") { out_id = VAR_GPS_LON_DEG; return true; }
    if (token == "gps_fix")     { out_id = VAR_GPS_FIX; return true; }
    if (token == "pressure_hPa"){ out_id = VAR_PRESSURE_HPA; return true; }
    if (token == "temp_C")      { out_id = VAR_TEMP_C; return true; }
    if (token == "humidity_pct"){ out_id = VAR_HUMIDITY_PCT; return true; }
    return false;
}

/**
 * @brief Parse an operator token from HTML into CompareOp.
 *
 * HTML values: gt, gte, eq, lte, lt
 */
static bool parseOp(const String& token, uint8_t& out_op) {
    if (token == "gt")  { out_op = OP_GT;  return true; }
    if (token == "gte") { out_op = OP_GTE; return true; }
    if (token == "eq")  { out_op = OP_EQ;  return true; }
    if (token == "lte") { out_op = OP_LTE; return true; }
    if (token == "lt")  { out_op = OP_LT;  return true; }
    return false;
}

/**
 * @brief Return true if float is finite.
 */
static bool isFiniteFloat(float x) {
    return isfinite(x);
}

// -------------------------
// Internal helpers: banner injection
// -------------------------

/**
 * @brief Escape a small banner string for safe HTML embedding.
 *
 * This is a minimal escaper: &, <, >, " are replaced.
 */
static String htmlEscape(const char* s) {
    if (!s) return String();
    String out;
    for (const char* p = s; *p; ++p) {
        switch (*p) {
            case '&': out += F("&amp;"); break;
            case '<': out += F("&lt;"); break;
            case '>': out += F("&gt;"); break;
            case '"': out += F("&quot;"); break;
            default:  out += *p; break;
        }
    }
    return out;
}

/**
 * @brief Inject a banner div right after the opening <body ...> tag.
 *
 * If injection fails (no <body> tag found), the HTML is returned unmodified.
 */
static String injectBanner(const String& html, const char* banner_message, bool is_error) {
    if (!banner_message || !banner_message[0]) return html;

    const int body_pos = html.indexOf(F("<body"));
    if (body_pos < 0) return html;

    const int body_gt = html.indexOf('>', body_pos);
    if (body_gt < 0) return html;

    const String msg = htmlEscape(banner_message);

    String banner;
    banner.reserve(256);
    banner += F("<div style=\"margin:12px auto; max-width:1100px; padding:10px 12px; border-radius:6px; ");
    banner += is_error
        ? F("background:#7f1d1d; border:1px solid #b91c1c; color:#fee2e2;")
        : F("background:#14532d; border:1px solid #16a34a; color:#dcfce7;");
    banner += F("\">");
    banner += msg;
    banner += F("</div>");

    String out = html;
    out.reserve(html.length() + banner.length() + 16);
    out = html.substring(0, body_gt + 1) + banner + html.substring(body_gt + 1);
    return out;
}

/**
 * @brief Escape a C string for safe embedding in a JavaScript single-quoted string.
 *
 * This is used for pre-filling the settings page with current values.
 */
static String jsEscapeSingleQuoted(const char* s) {
    if (!s) return String();
    String out;
    for (const char* p = s; *p; ++p) {
        const char ch = *p;
        switch (ch) {
            case '\\': out += F("\\\\\\\\"); break;
            case '\'':   out += F("\\\\'");    break;
            case '\n':   out += F("\\\\n");    break;
            case '\r':   out += F("\\\\r");    break;
            case '\t':   out += F("\\\\t");    break;
            default:
                // Avoid raw control chars
                if ((uint8_t)ch < 0x20) {
                    out += ' ';
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

/**
 * @brief Convert VariableId into the HTML token used in <select> values.
 *
 * Must match the tokens in SETTINGS_PAGE_HTML:
 *  t_power_s, t_launch_s, gps_alt_m, gps_lat_deg, gps_lon_deg, gps_fix,
 *  pressure_hPa, temp_C, humidity_pct
 */
static const char* varIdToToken(uint8_t var_id) {
    switch (var_id) {
        case VAR_T_POWER_S:    return "t_power_s";
        case VAR_T_LAUNCH_S:   return "t_launch_s";
        case VAR_GPS_ALT_M:    return "gps_alt_m";
        case VAR_GPS_LAT_DEG:  return "gps_lat_deg";
        case VAR_GPS_LON_DEG:  return "gps_lon_deg";
        case VAR_GPS_FIX:      return "gps_fix";
        case VAR_PRESSURE_HPA: return "pressure_hPa";
        case VAR_TEMP_C:       return "temp_C";
        case VAR_HUMIDITY_PCT: return "humidity_pct";
        default:               return "t_power_s";
    }
}

/**
 * @brief Convert CompareOp into the HTML token used in <select> values.
 *
 * Must match: gt, gte, eq, lte, lt
 */
static const char* opToToken(uint8_t op) {
    switch (op) {
        case OP_GT:  return "gt";
        case OP_GTE: return "gte";
        case OP_EQ:  return "eq";
        case OP_LTE: return "lte";
        case OP_LT:  return "lt";
        default:     return "gt";
    }
}

/**
 * @brief Build a JavaScript snippet that pre-fills the HTML form from g_settings.
 *
 * Strategy:
 *  - We do NOT try to rewrite the HTML with string replaces (fragile).
 *  - Instead we inject a small <script> before </body> that sets values by name/id.
 *  - This keeps your HTML intact and makes later UI changes easy.
 */
static String buildPrefillScript(const SystemConfig& cfg) {
    String js;
    js.reserve(8192);

    js += F("<script>(function(){\n");
    js += F("function setValById(id,v){var e=document.getElementById(id); if(e){e.value=v;}}\n");
    js += F("function setTxtById(id,t){var e=document.getElementById(id); if(e){e.textContent=t;}}\n");
    js += F("function setCheck(name,checked){var e=document.querySelector('input[type=\"checkbox\"][name=\"'+name+'\"]'); if(e){e.checked=!!checked;}}\n");
    js += F("function setSelect(name,val){var e=document.querySelector('select[name=\"'+name+'\"]'); if(e){e.value=val;}}\n");
    js += F("function setNumber(name,val){var e=document.querySelector('input[type=\"number\"][name=\"'+name+'\"]'); if(e){e.value=val;}}\n");
    js += F("function setText(name,val){var e=document.querySelector('input[type=\"text\"][name=\"'+name+'\"]'); if(e){e.value=val;}}\n");
    js += F("function setPassword(name,val){var e=document.querySelector('input[type=\"password\"][name=\"'+name+'\"]'); if(e){e.value=val;}}\n");

    // Firmware / header display (best-effort; IDs may not exist)
    char fw[24];
    snprintf(fw, sizeof(fw), "v%u.%u.%u",
             (unsigned)FW_VERSION_MAJOR,
             (unsigned)FW_VERSION_MINOR,
             (unsigned)FW_VERSION_PATCH);
    js += F("setTxtById('fwVersion','");
    js += jsEscapeSingleQuoted(fw);
    js += F("');\n");

    // Device name pill: show CONFIG-<serial> for now
    char dev[32];
    snprintf(dev, sizeof(dev), "CONFIG-%lu", (unsigned long)cfg.device.serial_number);
    js += F("setTxtById('deviceName','");
    js += jsEscapeSingleQuoted(dev);
    js += F("');\n");

    // Device/WiFi
    // Device/WiFi
js += F("setPassword('ap_password','");
js += jsEscapeSingleQuoted(cfg.device.ap_password);
js += F("');\n");

// Global cut toggles (UI only exposes require_launch; require_fix is forced false)
js += F("setCheck('gc_require_launch',");
js += (cfg.global_cutdown.require_launch_before_cut ? "1" : "0");
js += F(");\n");

// Termination detector (balloon-pop / descent detector)
js += F("setCheck('term_enabled',");
js += (cfg.term.enabled ? "1" : "0");
js += F(");\n");
js += F("setNumber('term_sustain_s','");
js += String((unsigned)cfg.term.sustain_s);
js += F("');\n");
js += F("setCheck('term_use_gps',");
js += (cfg.term.use_gps ? "1" : "0");
js += F(");\n");
js += F("setNumber('term_gps_drop_m','");
js += String(cfg.term.gps_drop_m, 2);
js += F("');\n");
js += F("setCheck('term_use_pressure',");
js += (cfg.term.use_pressure ? "1" : "0");
js += F(");\n");
js += F("setNumber('term_pressure_rise_hpa','");
js += String(cfg.term.pressure_rise_hpa, 1);
js += F("');\n");

    // External inputs
    for (uint8_t i = 0; i < NUM_EXTERNAL_INPUTS; i++) {
        js += F("setCheck('ext");
        js += String(i);
        js += F("_enabled',");
        js += (cfg.external_inputs[i].enabled ? "1" : "0");
        js += F(");\n");

        js += F("setSelect('ext");
        js += String(i);
        js += F("_active_high','");
        js += (cfg.external_inputs[i].active_high ? "1" : "0");
        js += F("');\n");

        js += F("setNumber('ext");
        js += String(i);
        js += F("_debounce_ms','");
        js += String((unsigned)cfg.external_inputs[i].debounce_ms);
        js += F("');\n");
    }

    // Iridium
    js += F("setCheck('ir_enabled',");
    js += (cfg.iridium.enabled ? "1" : "0");
    js += F(");\n");
    js += F("setCheck('ir_remote_cut',");
    js += (cfg.iridium.cutdown_on_command ? "1" : "0");
    js += F(");\n");
    js += F("setText('ir_token','");
    js += jsEscapeSingleQuoted(cfg.iridium.cutdown_token);
    js += F("');\n");

    js += F("setNumber('ir_ground_s','");
    js += String((unsigned long)cfg.iridium.ground_interval_s);
    js += F("');\n");

    js += F("setNumber('ir_ascent_s','");
    js += String((unsigned long)cfg.iridium.ascent_interval_s);
    js += F("');\n");

    js += F("setNumber('ir_descent_s','");
    js += String((unsigned long)cfg.iridium.descent_interval_s);
    js += F("');\n");

    js += F("setNumber('ir_descent_dur_s','");
    js += String((unsigned long)cfg.iridium.descent_duration_s);
    js += F("');\n");

    js += F("setNumber('ir_beacon_s','");
    js += String((unsigned long)cfg.iridium.beacon_interval_s);
    js += F("');\n");

    // Bucket A/B
    for (uint8_t i = 0; i < MAX_BUCKET_CONDITIONS; i++) {
        const Condition& a = cfg.bucketA[i];
        js += F("setCheck('a");
        js += String(i);
        js += F("_enabled',");
        js += (a.enabled ? "1" : "0");
        js += F(");\n");
        js += F("setSelect('a");
        js += String(i);
        js += F("_var','");
        js += varIdToToken(a.var_id);
        js += F("');\n");
        js += F("setSelect('a");
        js += String(i);
        js += F("_op','");
        js += opToToken(a.op);
        js += F("');\n");
        js += F("setNumber('a");
        js += String(i);
        js += F("_value','");
        js += String(a.value, 6);
        js += F("');\n");
        js += F("setNumber('a");
        js += String(i);
        js += F("_for_s','");
        js += String((unsigned)a.for_seconds);
        js += F("');\n");

        const Condition& b = cfg.bucketB[i];
        js += F("setCheck('b");
        js += String(i);
        js += F("_enabled',");
        js += (b.enabled ? "1" : "0");
        js += F(");\n");
        js += F("setSelect('b");
        js += String(i);
        js += F("_var','");
        js += varIdToToken(b.var_id);
        js += F("');\n");
        js += F("setSelect('b");
        js += String(i);
        js += F("_op','");
        js += opToToken(b.op);
        js += F("');\n");
        js += F("setNumber('b");
        js += String(i);
        js += F("_value','");
        js += String(b.value, 6);
        js += F("');\n");
        js += F("setNumber('b");
        js += String(i);
        js += F("_for_s','");
        js += String((unsigned)b.for_seconds);
        js += F("');\n");
    }

    js += F("})();</script>\n");
    return js;
}

/**
 * @brief Inject the prefill JS before </body>.
 */
static String injectPrefill(const String& html, const SystemConfig& cfg) {
    const int pos = html.lastIndexOf(F("</body>"));
    if (pos < 0) {
        // If </body> isn't found, append at end as best-effort.
        return html + buildPrefillScript(cfg);
    }
    String out;
    const String script = buildPrefillScript(cfg);
    out.reserve(html.length() + script.length() + 16);
    out = html.substring(0, pos) + script + html.substring(pos);
    return out;
}

/**
 * @brief Send the settings page HTML, with optional banner, and pre-filled current settings.
 */
static void handleStatusJson();


static const char* modeToString(SystemMode m) {
    switch (m) {
        case MODE_NORMAL: return "NORMAL";
        case MODE_CONFIG: return "CONFIG";
        default: return "UNKNOWN";
    }
}

static void handleStatusJson() {
    // Minimal JSON for live view (no ArduinoJson dependency).
    // Values may be null when invalid.
    char buf[768];

    auto numOrNull = [](bool valid, double v, char* out, size_t out_len, const char* fmt) {
        if (!valid) {
            strncpy(out, "null", out_len);
            out[out_len - 1] = '\0';
            return;
        }
        snprintf(out, out_len, fmt, v);
    };

    char lat[24], lon[24], alt[24];
    char p[24], t[24], h[24];

    numOrNull(g_readings.gps_lat_valid, g_readings.gps_lat_deg, lat, sizeof(lat), "%.6f");
    numOrNull(g_readings.gps_lon_valid, g_readings.gps_lon_deg, lon, sizeof(lon), "%.6f");
    numOrNull(g_readings.gps_alt_valid, g_readings.gps_alt_m,   alt, sizeof(alt), "%.1f");

    numOrNull(g_readings.pressure_valid,  g_readings.pressure_hpa, p, sizeof(p), "%.1f");
    numOrNull(g_readings.temp_valid,      g_readings.temp_c,       t, sizeof(t), "%.1f");
    numOrNull(g_readings.humidity_valid,  g_readings.humidity_pct, h, sizeof(h), "%.1f");

    // Next Iridium TX countdown isn't tracked precisely in config mode; report -1.
    const int ir_next_s = -1;

    const char* cut_reason = "none";
    // If you later store a cut reason string/enum, wire it here.

    const int n = snprintf(
        buf, sizeof(buf),
        "{"
          "\"mode\":\"%s\","
          "\"t_power_s\":%lu,"
          "\"t_launch_s\":%lu,"
          "\"gps_fix\":%s,"
          "\"gps_lat\":%s,"
          "\"gps_lon\":%s,"
          "\"gps_alt\":%s,"
          "\"pressure_hpa\":%s,"
          "\"temp_c\":%s,"
          "\"humidity_pct\":%s,"
          "\"iridium_next_s\":%d,"
          "\"last_cut_reason\":\"%s\""
        "}",
        modeToString(g_state.system_mode),
        (unsigned long)g_state.t_power_s,
        (unsigned long)g_state.t_launch_s,
        (g_readings.gps_fix_valid && g_readings.gps_fix) ? "true" : "false",
        lat, lon, alt,
        p, t, h,
        ir_next_s,
        cut_reason
    );

    if (n < 0 || (size_t)n >= sizeof(buf)) {
        g_server->send(500, "text/plain", "status.json overflow");
        return;
    }

    g_server->sendHeader("Cache-Control", "no-store");
    g_server->send(200, "application/json", buf);
}


static void sendSettingsPage(const char* banner_message, bool is_error) {
    if (!g_server) return;

    if (banner_message && banner_message[0]) {
        g_server->sendHeader("X-SGCP-Message", banner_message);
    }

    String html = FPSTR(SETTINGS_PAGE_HTML);

    // Visible banner (optional)
    html = injectBanner(html, banner_message, is_error);

    // Prefill current values
    html = injectPrefill(html, g_settings);

    g_server->send(200, "text/html", html);
}

// -------------------------
// Validation implementation (expanded)
// -------------------------

void webconfigValidateCandidate(const SystemConfig& candidate, WebConfigValidationResult& out_result) {
    /**
     * Validate a candidate configuration parsed from a form POST.
     *
     * Goals:
     *  - Prevent bricking config mode (bad AP password)
     *  - Reject obvious nonsense / corruption (NaNs, unknown enums)
     *  - Provide useful, compact feedback in out_result.summary
     */
    out_result.ok = true;
    out_result.error_count = 0;
    out_result.summary[0] = '\0';

    auto addError = [&](const char* msg) {
        out_result.ok = false;
        out_result.error_count++;
        if (out_result.summary[0] == '\0') {
            snprintf(out_result.summary, sizeof(out_result.summary), "%s", msg);
        } else {
            const size_t used = strlen(out_result.summary);
            snprintf(out_result.summary + used,
                     (used < sizeof(out_result.summary)) ? (sizeof(out_result.summary) - used) : 0,
                     "; %s", msg);
        }
        out_result.summary[sizeof(out_result.summary) - 1] = '\0';
    };

    // Serial number: 0 allowed (unassigned), max 7 digits.
    if (candidate.device.serial_number > 9999999u) addError("Serial number must be 0..9999999");

    // AP password: WPA2 requires >= 8 characters.
    // (candidate is const; we assume strings were copied with termination already.)
    if (strlen(candidate.device.ap_password) < 8) addError("AP password must be at least 8 characters");

    // External inputs: debounce sanity.
    for (uint8_t i = 0; i < NUM_EXTERNAL_INPUTS; i++) {
        if (candidate.external_inputs[i].debounce_ms > 5000) {
            addError("External input debounce must be <= 5000 ms");
            break;
        }
    }

    // Iridium: intervals sanity when enabled.
    if (candidate.iridium.enabled) {
        if (candidate.iridium.ascent_interval_s < 10) addError("Iridium ascent interval must be >= 10 s");
        if (candidate.iridium.descent_interval_s != 0 && candidate.iridium.descent_interval_s < 10) addError("Iridium descent interval must be >= 10 s");
        if (candidate.iridium.beacon_interval_s != 0 && candidate.iridium.beacon_interval_s < 10) addError("Iridium beacon interval must be >= 10 s");
        if (candidate.iridium.descent_duration_s != 0 && candidate.iridium.descent_duration_s < 10) {
            addError("Iridium descent duration must be 0 or >= 10 s");
        }
        // Token: allow empty if remote cut disabled; otherwise require non-empty.
        if (candidate.iridium.cutdown_on_command && candidate.iridium.cutdown_token[0] == '\0') {
            addError("Iridium remote cut token cannot be empty when remote cut is enabled");
        }
    }

    // Buckets: validate enabled conditions.
    auto validateCond = [&](const Condition& c, const char* label) {
        if (!c.enabled) return;

        if (c.var_id >= VAR__COUNT) {
            addError(label);
            return;
        }
        if (c.op > OP_GT) {
            addError(label);
            return;
        }
        if (!isFiniteFloat(c.value)) {
            addError(label);
            return;
        }
        // for_seconds is uint16_t. We can clamp later; here we just accept.
        // Optional variable-specific guardrails:
        if (c.var_id == VAR_GPS_LAT_DEG && (c.value < -90.0f || c.value > 90.0f)) {
            addError("Bucket condition has latitude value outside [-90,90]");
        }
        if (c.var_id == VAR_GPS_LON_DEG && (c.value < -180.0f || c.value > 180.0f)) {
            addError("Bucket condition has longitude value outside [-180,180]");
        }
        if (c.var_id == VAR_HUMIDITY_PCT && (c.value < 0.0f || c.value > 100.0f)) {
            addError("Bucket condition has humidity value outside [0,100]");
        }
    };

    for (uint8_t i = 0; i < MAX_BUCKET_CONDITIONS; i++) {
        validateCond(candidate.bucketA[i], "Bucket A condition invalid");
        validateCond(candidate.bucketB[i], "Bucket B condition invalid");
        if (out_result.error_count >= 8) break; // keep summary sane
    }

    // FieldWatch: allow empty strings even if enabled (you said not that sensitive).
    // If you want, we can warn (not fail) later.
    (void)candidate.fieldwatch;
}

// -------------------------
// Routes: parsing / saving
// -------------------------

static bool g_saved_ok = false;
static bool g_exit_requested = false;
static bool g_defaults_requested = false;

/**
 * @brief Parse one condition row from POST fields into a Condition.
 *
 * @param prefix 'a' for bucketA, 'b' for bucketB
 * @param idx 0..MAX_BUCKET_CONDITIONS-1
 * @param c Condition to fill
 */
static void parseConditionRow(char prefix, uint8_t idx, Condition& c) {
    if (!g_server) return;

    char key[24];

    // enabled
    snprintf(key, sizeof(key), "%c%u_enabled", prefix, (unsigned)idx);
    c.enabled = g_server->hasArg(key) ? toBool(g_server->arg(key), false) : false;

    // var
    snprintf(key, sizeof(key), "%c%u_var", prefix, (unsigned)idx);
    if (g_server->hasArg(key)) {
        uint8_t vid = c.var_id;
        if (parseVarId(g_server->arg(key), vid)) c.var_id = vid;
    }

    // op
    snprintf(key, sizeof(key), "%c%u_op", prefix, (unsigned)idx);
    if (g_server->hasArg(key)) {
        uint8_t op = c.op;
        if (parseOp(g_server->arg(key), op)) c.op = op;
    }

    // value
    snprintf(key, sizeof(key), "%c%u_value", prefix, (unsigned)idx);
    if (g_server->hasArg(key)) {
        c.value = toF32(g_server->arg(key), c.value);
    }

    // dwell seconds
    snprintf(key, sizeof(key), "%c%u_for_s", prefix, (unsigned)idx);
    if (g_server->hasArg(key)) {
        c.for_seconds = toU16(g_server->arg(key), c.for_seconds);
    }

    // runtime accumulator is always reset on load/save paths elsewhere in firmware.
    c.true_duration_s = 0.0f;
}

/**
 * @brief Build a candidate config from current settings, then apply form fields.
 */
static void applyFormToCandidate(SystemConfig& candidate) {
    if (!g_server) return;

    // Device/AP password (serial number is factory-set and not editable)
    if (g_server->hasArg("ap_password")) {
        copyArgToBuf(candidate.device.ap_password, sizeof(candidate.device.ap_password), g_server->arg("ap_password"));
    }

    // Global cut toggles (UI only exposes require_launch; require_fix is forced false)
    if (g_server->hasArg("gc_require_launch")) {
        candidate.global_cutdown.require_launch_before_cut =
            toBool(g_server->arg("gc_require_launch"), candidate.global_cutdown.require_launch_before_cut);
    }

    // Termination detector (term_*)
    if (g_server->hasArg("term_enabled")) {
        candidate.term.enabled = toBool(g_server->arg("term_enabled"), candidate.term.enabled);
    }
    if (g_server->hasArg("term_sustain_s")) {
        candidate.term.sustain_s = toU16(g_server->arg("term_sustain_s"), candidate.term.sustain_s);
    }
    if (g_server->hasArg("term_use_gps")) {
        candidate.term.use_gps = toBool(g_server->arg("term_use_gps"), candidate.term.use_gps);
    }
    if (g_server->hasArg("term_gps_drop_m")) {
        candidate.term.gps_drop_m = toF32(g_server->arg("term_gps_drop_m"), candidate.term.gps_drop_m);
    }
    if (g_server->hasArg("term_use_pressure")) {
        candidate.term.use_pressure = toBool(g_server->arg("term_use_pressure"), candidate.term.use_pressure);
    }
    if (g_server->hasArg("term_pressure_rise_hpa")) {
        candidate.term.pressure_rise_hpa = toF32(g_server->arg("term_pressure_rise_hpa"), candidate.term.pressure_rise_hpa);
    }

    // External inputs: ext0_enabled/ext0_active_high/ext0_debounce_ms, ext1_...
    for (uint8_t i = 0; i < NUM_EXTERNAL_INPUTS; i++) {
        char k[24];

        snprintf(k, sizeof(k), "ext%u_enabled", (unsigned)i);
        if (g_server->hasArg(k)) candidate.external_inputs[i].enabled = toBool(g_server->arg(k), candidate.external_inputs[i].enabled);

        snprintf(k, sizeof(k), "ext%u_active_high", (unsigned)i);
        if (g_server->hasArg(k)) candidate.external_inputs[i].active_high = toBool(g_server->arg(k), candidate.external_inputs[i].active_high);

        snprintf(k, sizeof(k), "ext%u_debounce_ms", (unsigned)i);
        if (g_server->hasArg(k)) candidate.external_inputs[i].debounce_ms = toU16(g_server->arg(k), candidate.external_inputs[i].debounce_ms);
    }

    // Iridium
    if (g_server->hasArg("ir_enabled")) {
        candidate.iridium.enabled = toBool(g_server->arg("ir_enabled"), candidate.iridium.enabled);
    }
    if (g_server->hasArg("ir_remote_cut")) {
        candidate.iridium.cutdown_on_command = toBool(g_server->arg("ir_remote_cut"), candidate.iridium.cutdown_on_command);
    }
    if (g_server->hasArg("ir_token")) {
        copyArgToBuf(candidate.iridium.cutdown_token, sizeof(candidate.iridium.cutdown_token), g_server->arg("ir_token"));
    }
    if (g_server->hasArg("ir_ground_s")) {
        candidate.iridium.ground_interval_s = toU32(g_server->arg("ir_ground_s"), candidate.iridium.ground_interval_s);
    }
    if (g_server->hasArg("ir_ascent_s")) {
        candidate.iridium.ascent_interval_s = toU32(g_server->arg("ir_ascent_s"), candidate.iridium.ascent_interval_s);
    }
    if (g_server->hasArg("ir_descent_s")) {
        candidate.iridium.descent_interval_s = toU32(g_server->arg("ir_descent_s"), candidate.iridium.descent_interval_s);
    }
    if (g_server->hasArg("ir_descent_dur_s")) {
        candidate.iridium.descent_duration_s = toU32(g_server->arg("ir_descent_dur_s"), candidate.iridium.descent_duration_s);
    }
    if (g_server->hasArg("ir_beacon_s")) {
        candidate.iridium.beacon_interval_s = toU32(g_server->arg("ir_beacon_s"), candidate.iridium.beacon_interval_s);
    }

    // Bucket A / B rows
    for (uint8_t i = 0; i < MAX_BUCKET_CONDITIONS; i++) {
        parseConditionRow('a', i, candidate.bucketA[i]);
        parseConditionRow('b', i, candidate.bucketB[i]);
    }

    // UI does not expose this; keep it forced false for v1.
    candidate.global_cutdown.require_gps_fix_before_cut = false;

    // Hygiene: ensure strings terminated
    ensureNullTerminated(candidate.device.ap_password, sizeof(candidate.device.ap_password));
    ensureNullTerminated(candidate.iridium.cutdown_token, sizeof(candidate.iridium.cutdown_token));
    ensureNullTerminated(candidate.fieldwatch.device_id, sizeof(candidate.fieldwatch.device_id));
    ensureNullTerminated(candidate.fieldwatch.access_token, sizeof(candidate.fieldwatch.access_token));
}

/**
 * @brief Handler for POST /save
 */
static void handleSave() {
    if (!g_server) return;

    SystemConfig candidate = g_settings;
    applyFormToCandidate(candidate);

    WebConfigValidationResult vr;
    webconfigValidateCandidate(candidate, vr);

    if (!vr.ok) {
        g_saved_ok = false;
        sendSettingsPage(vr.summary[0] ? vr.summary : "Validation failed", true);
        return;
    }

    g_settings = candidate;
    if (!settingsSave()) {
        g_saved_ok = false;
        sendSettingsPage("Save failed (NVS write)", true);
        return;
    }

    g_saved_ok = true;
    sendSettingsPage("Saved OK - restarting...", false);
}

/**
 * @brief Handler for POST /exit
 */
static void handleExit() {
    if (!g_server) return;
    g_exit_requested = true;
    sendSettingsPage("Exiting - restarting...", false);
}

/**
 * @brief Handler for POST /defaults
 */
static void handleDefaults() {
    if (!g_server) return;
    g_defaults_requested = true;
    sendSettingsPage("Restoring defaults (serial preserved) - restarting...", false);
}

/**
 * @brief Handler for POST /lock
 *
 * Stub until servo_release module exists.
 */
static void handleLock() {
    if (!g_server) return;
    sendSettingsPage("Lock command received (release mechanism not wired yet).", false);
}

/**
 * @brief Handler for POST /release
 *
 * Stub until servo_release module exists.
 */
static void handleRelease() {
    if (!g_server) return;
    sendSettingsPage("Release command received (release mechanism not wired yet).", false);
}

/**
 * @brief Factory-only route: set device serial number
 * Format: /factory/setSerial?sn=12345678
 * NOTE: You said no token is needed.
 */
static void handleFactorySetSerial() {
    if (!g_server) return;

    // 1) Validate parameter
    if (!g_server->hasArg("sn")) {
        g_server->send(400, "text/plain", "Missing sn");
        return;
    }

    // 2) Parse serial number (decimal)
    const String snStr = g_server->arg("sn");
    char* endp = nullptr;
    uint32_t newSn = (uint32_t)strtoul(snStr.c_str(), &endp, 10);

    // Validate parse
    if (endp == snStr.c_str() || *endp != '\0') {
        g_server->send(400, "text/plain", "Invalid sn (must be decimal integer)");
        return;
    }

    // Optional (recommended): disallow SN=0
    if (newSn == 0) {
        g_server->send(400, "text/plain", "Invalid sn (cannot be 0)");
        return;
    }

    // Optional (recommended): write-once guard
    // If you want SN to be settable only once, uncomment this.
    /*
    if (g_settings.device.serial_number != 0) {
        g_server->send(403, "text/plain", "Serial already set");
        return;
    }
    */

    // 3) Save
    g_settings.device.serial_number = newSn;
    if (!settingsSave()) {
        g_server->send(500, "text/plain", "Failed to save serial number");
        return;
    }

    // 4) Confirm + reboot using existing config-mode flow
    char msg[128];
    snprintf(msg, sizeof(msg),
             "Serial set to %lu. Rebooting...",
             (unsigned long)newSn);

    g_server->send(200, "text/plain", msg);

    // This will cause the config-mode loop to exit and restart cleanly
    g_saved_ok = true;
}

// -------------------------
// OTA upload handler (FieldTemp-style)
// -------------------------

static void setupFirmwareRoutes(WebServer& server) {
    server.on("/firmware", HTTP_GET, [&]() {
        server.send_P(200, "text/html", FIRMWARE_PAGE_HTML);
    });

    server.on(
        "/firmware",
        HTTP_POST,
        [&]() {
            if (Update.hasError()) {
                server.send(500, "text/plain", "Update failed.");
                return;
            }
            server.send(200, "text/plain", "Update OK. Restarting...");
            delay(250);
            ESP.restart();
        },
        [&]() {
            HTTPUpload& upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (!Update.end(true)) {
                    Update.printError(Serial);
                }
            }
        }
    );
}

// -------------------------
// Public API implementation
// -------------------------

void webconfigInit() {
    /**
     * Initialize config button GPIO.
     *
     * Config button is pulled up and reads LOW when pressed.
     */
    pinMode(PIN_CONFIG_BUTTON, INPUT_PULLUP);

    g_cfg_btn.last_raw = configButtonPressedRaw();
    g_cfg_btn.stable_pressed = g_cfg_btn.last_raw;
    g_cfg_btn.last_change_ms = millis();
    g_press_start_ms = 0;
}

WebConfigOptions webconfigGetDefaultOptions() {
    WebConfigOptions opts;
    opts.config_timeout_ms = CONFIG_MODE_TIMEOUT_MS;
    opts.http_port = 80;
    opts.enable_ota = true;
    return opts;
}

void webconfigSetOptions(const WebConfigOptions& opts) {
    g_webcfg_opts = opts;
}

void webconfigFormatSsid(char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    snprintf(out, out_len, "CONFIG-%lu", (unsigned long)g_settings.device.serial_number);
    out[out_len - 1] = '\0';
}

bool webconfigCheckHoldAtBoot(uint32_t hold_ms) {
    /**
     * If the config button is held at boot for hold_ms, perform factory reset and reboot.
     *
     * Important requirement:
     *  - Factory reset must NOT modify serial number. (Handled inside settingsResetToDefaultsAndSave()).
     */
    const uint32_t start_ms = millis();

    if (!configButtonPressedRaw()) {
        return false;
    }

    while (configButtonPressedRaw()) {
        const uint32_t now_ms = millis();
        if ((now_ms - start_ms) >= hold_ms) {
            (void)settingsResetToDefaultsAndSave();
            ESP.restart();
            return true;
        }
        delay(5);
    }

    return false;
}

bool webconfigPollButton() {
    const uint32_t now_ms = millis();

    bool pressed_edge = false;
    bool released_edge = false;
    updateDebouncedButton(g_cfg_btn, now_ms, CONFIG_BTN_DEBOUNCE_MS, pressed_edge, released_edge);

    if (pressed_edge) {
        g_press_start_ms = now_ms;
    }

    if (released_edge) {
        const uint32_t press_dur = (g_press_start_ms == 0) ? 0 : (now_ms - g_press_start_ms);
        g_press_start_ms = 0;

        if (press_dur >= CONFIG_BTN_MIN_PRESS_MS) {
            webconfigEnter(); // blocking; restarts on exit
            return true;
        }
    }

    return false;
}

void webconfigEnter() {
    /**
     * Enter configuration mode (blocking).
     *
     * Behavior:
     *  - Start AP
     *  - Start WebServer + routes
     *  - Handle requests until:
     *      - Save succeeded -> restart
     *      - Exit -> restart
     *      - Defaults -> reset defaults + restart
     *      - Timeout without successful Save -> restart
     */
    char ssid[32];
    webconfigFormatSsid(ssid, sizeof(ssid));

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, g_settings.device.ap_password);
    delay(250);

    WebServer server(g_webcfg_opts.http_port);
    g_server = &server;

    g_saved_ok = false;
    g_exit_requested = false;
    g_defaults_requested = false;

    // Routes
    // Routes
    server.on("/", HTTP_GET, [&]() {
        sendSettingsPage(nullptr, false);
    });

    server.on("/status.json", HTTP_GET, [&]() {
        handleStatusJson();
    });


    server.on("/save", HTTP_POST, [&]() {
        handleSave();
    });

    server.on("/factory/setSerial", HTTP_GET, [&]() {
    handleFactorySetSerial();
    });

    server.on("/exit", HTTP_POST, [&]() {
        handleExit();
    });

    server.on("/defaults", HTTP_POST, [&]() {
        handleDefaults();
    });

    server.on("/lock", HTTP_POST, [&]() {
        handleLock();
    });

    server.on("/release", HTTP_POST, [&]() {
        handleRelease();
    });

    if (g_webcfg_opts.enable_ota) {
        setupFirmwareRoutes(server);
    }

    server.begin();

    const uint32_t start_ms = millis();

    while (true) {
        const uint32_t now_ms = millis();

        // Keep GPS UART drained so live view stays responsive.
        readingsDrainGPS();

        // Update sensor snapshot at ~1 Hz while in config mode.
        static uint32_t next_readings_ms = 0;
        if ((int32_t)(now_ms - next_readings_ms) >= 0) {
            next_readings_ms = now_ms + 1000;
            readingsUpdate1Hz(now_ms);
        }

        server.handleClient();
        delay(SERVER_LOOP_DELAY_MS);

        // Save succeeded
        if (g_saved_ok) {
            delay(250);
            server.stop();
            WiFi.softAPdisconnect(true);
            g_server = nullptr;
            ESP.restart();
            return;
        }

        // Exit requested
        if (g_exit_requested) {
            delay(250);
            server.stop();
            WiFi.softAPdisconnect(true);
            g_server = nullptr;
            ESP.restart();
            return;
        }

        // Defaults requested
        if (g_defaults_requested) {
            // Restore defaults (serial preserved in settings module), then restart.
            (void)settingsResetToDefaultsAndSave();
            delay(250);
            server.stop();
            WiFi.softAPdisconnect(true);
            g_server = nullptr;
            ESP.restart();
            return;
        }

        // Timeout restart (no save)
        const uint32_t elapsed = millis() - start_ms;
        if (elapsed >= g_webcfg_opts.config_timeout_ms) {
            server.stop();
            WiFi.softAPdisconnect(true);
            g_server = nullptr;
            ESP.restart();
            return;
        }
    }
}

