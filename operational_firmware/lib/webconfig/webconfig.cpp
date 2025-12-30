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

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

#include <string.h>
#include <math.h>

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

/**
 * @brief Settings page HTML.
 *
 * IMPORTANT:
 *  - Replace the placeholder with your updated named-field HTML (the version with name="..." attributes).
 *  - Keep it intact. This code injects a banner at runtime without altering your source HTML.
 */
static const char SETTINGS_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>SkyGuard Cutdown Pro – Configuration</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">

  <style>
    :root {
      /* Dark theme with orange company accent */
      --bg: #020617;           /* page background */
      --bg-panel: #0b1120;     /* card background */
      --bg-panel-alt: #020617;
      --border: #1f2937;
      --border-soft: #111827;
      --accent: #f97316;       /* orange accent */
      --accent-soft: #451a03;
      --accent-dark: #c2410c;
      --text: #e5e7eb;
      --text-muted: #9ca3af;
      --header-bg: #020617;
      --header-text: #e5e7eb;
      --radius: 6px;
      --shadow: 0 1px 3px rgba(15, 23, 42, 0.9);
      --font: -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif;
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      padding: 0;
      font-family: var(--font);
      background: radial-gradient(circle at top, #111827 0, #020617 55%);
      color: var(--text);
      -webkit-text-size-adjust: 100%;
    }

    .page {
      max-width: 1100px;
      margin: 0 auto;
      padding: 12px 12px 20px;
    }

    header {
      background: linear-gradient(135deg, #020617, #0b1120);
      color: var(--header-text);
      border-radius: var(--radius);
      padding: 10px 12px;
      box-shadow: var(--shadow);
      margin-bottom: 10px;
      border: 1px solid #1f2937;
    }

    header h1 {
      font-size: 1.2rem;
      margin: 0 0 4px;
    }

    header .subtitle {
      font-size: 0.85rem;
      color: #9ca3af;
    }

    header .status-bar {
      display: flex;
      flex-wrap: wrap;
      gap: 6px;
      margin-top: 6px;
      font-size: 0.75rem;
    }

    .pill-group {
      display: inline-flex;
      border-radius: 999px;
      border: 1px solid rgba(148, 163, 184, 0.6);
      overflow: hidden;
    }

    .pill-group span {
      padding: 2px 7px;
      white-space: nowrap;
    }

    .pill-label {
      background: rgba(15, 23, 42, 0.95);
      font-weight: 500;
      color: #e5e7eb;
    }

    .pill-value {
      background: rgba(30, 64, 175, 0.4);
      color: #e5e7eb;
    }

    main {
      margin-top: 10px;
      display: flex;
      flex-direction: column;
      gap: 10px;
    }

    .section {
      background: radial-gradient(circle at top left, #111827 0, #020617 55%);
      border-radius: var(--radius);
      border: 1px solid var(--border);
      box-shadow: var(--shadow);
      padding: 10px 10px 8px;
    }

    .section h2 {
      font-size: 1rem;
      margin: 0 0 6px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 8px;
    }

    .section h2 .tag {
      font-size: 0.7rem;
      border-radius: 999px;
      border: 1px solid var(--border-soft);
      padding: 1px 6px;
      color: var(--text-muted);
      background: #020617;
      white-space: nowrap;
    }

    .section p.help {
      margin: 0 0 6px;
      font-size: 0.8rem;
      color: var(--text-muted);
    }

    .section-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 8px 12px;
    }

    .field {
      display: flex;
      flex-direction: column;
      gap: 3px;
      margin-bottom: 8px;
      font-size: 0.85rem;
    }

    .field-inline {
      display: flex;
      align-items: center;
      gap: 8px;
      margin-bottom: 8px;
      font-size: 0.85rem;
      flex-wrap: wrap;
    }

    label {
      font-size: 0.85rem;
      font-weight: 500;
    }

    label span.muted {
      font-weight: 400;
      color: var(--text-muted);
      font-size: 0.78rem;
    }

    input[type="text"],
    input[type="number"],
    input[type="password"],
    select {
      padding: 5px 6px;
      border-radius: var(--radius);
      border: 1px solid var(--border);
      font-size: 0.9rem;
      width: 100%;
      max-width: 100%;
      background: #020617;
      color: var(--text);
    }

    input[type="number"] { -moz-appearance: textfield; }
    input[type="number"]::-webkit-outer-spin-button,
    input[type="number"]::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; }

    select {
      -webkit-appearance: none;
      -moz-appearance: none;
      appearance: none;
      background-image:
        linear-gradient(45deg, transparent 50%, #9ca3af 50%),
        linear-gradient(135deg, #9ca3af 50%, transparent 50%);
      background-position:
        calc(100% - 14px) 9px,
        calc(100% - 10px) 9px;
      background-size: 4px 4px, 4px 4px;
      background-repeat: no-repeat;
      padding-right: 22px;
    }

    .conditions-table {
      width: 100%;
      border-collapse: collapse;
      margin-top: 4px;
      font-size: 0.78rem;
    }

    .conditions-table th,
    .conditions-table td {
      border: 1px solid var(--border);
      padding: 3px;
      text-align: left;
      vertical-align: middle;
      background: #020617;
    }

    .conditions-table th {
      background: #020617;
      font-weight: 600;
      color: #e5e7eb;
    }

    .conditions-table input[type="number"],
    .conditions-table select {
      width: 100%;
      font-size: 0.78rem;
      padding: 3px 4px;
      background: #020617;
      color: var(--text);
    }

    .conditions-table .cond-index {
      width: 26px;
      text-align: center;
      color: var(--text-muted);
    }

    .conditions-table .cond-enabled {
      width: 70px;
      text-align: center;
    }

    .conditions-note {
      margin-top: 4px;
      font-size: 0.78rem;
      color: var(--text-muted);
    }

    .button-row {
      display: flex;
      justify-content: flex-end;
      gap: 6px;
      margin-top: 6px;
      flex-wrap: wrap;
    }

    .btn {
      border-radius: var(--radius);
      border: 1px solid var(--border);
      background: #020617;
      padding: 6px 10px;
      font-size: 0.85rem;
      cursor: pointer;
      color: var(--text);
      -webkit-tap-highlight-color: transparent;
    }

    .btn-primary {
      background: var(--accent);
      border-color: var(--accent-dark);
      color: #020617;
      font-weight: 500;
    }

    .btn-danger {
      background: #7f1d1d;
      border-color: #b91c1c;
      color: #fee2e2;
    }

    .btn:active { transform: translateY(1px); }

    /* Toggle switch (iPhone-friendly, dark theme) */
    .toggle {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      cursor: pointer;
      -webkit-tap-highlight-color: transparent;
    }

    .toggle input {
      position: absolute;
      opacity: 0;
      width: 0;
      height: 0;
    }

    .toggle-track {
      position: relative;
      width: 46px;
      height: 24px;
      border-radius: 999px;
      background: #020617;
      border: 1px solid #4b5563;
      flex-shrink: 0;
      transition: background 0.2s ease, border-color 0.2s ease;
    }

    .toggle-thumb {
      position: absolute;
      top: 2px;
      left: 2px;
      width: 18px;
      height: 18px;
      border-radius: 999px;
      background: #e5e7eb;
      box-shadow: 0 1px 2px rgba(15, 23, 42, 0.7);
      transition: transform 0.2s ease, background 0.2s ease;
    }

    .toggle input:checked + .toggle-track {
      background: var(--accent);
      border-color: var(--accent-dark);
    }

    .toggle input:checked + .toggle-track .toggle-thumb {
      transform: translateX(20px);
      background: #020617;
    }

    .toggle-label { font-size: 0.85rem; }

    .toggle-subtext {
      font-size: 0.75rem;
      color: var(--text-muted);
      margin-left: 52px;
      margin-top: -2px;
    }

    .live-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 6px 10px;
      font-size: 0.8rem;
    }

    .live-item {
      padding: 4px 6px;
      border-radius: var(--radius);
      border: 1px solid var(--border-soft);
      background: #020617;
    }

    .live-label { font-weight: 500; margin-bottom: 2px; }

    .live-value {
      color: var(--text-muted);
      font-family: "SF Mono", ui-monospace, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
      font-size: 0.78rem;
      word-break: break-all;
    }

    @media (max-width: 640px) {
      header { padding: 8px 10px; }
      header h1 { font-size: 1.05rem; }
      main { gap: 8px; }
      .section { padding: 8px 8px 6px; }
      .conditions-table th, .conditions-table td { padding: 3px 2px; }
      .toggle-track { width: 44px; height: 22px; }
      .toggle-thumb { width: 16px; height: 16px; }
      .toggle input:checked + .toggle-track .toggle-thumb { transform: translateX(18px); }
      .toggle-subtext { margin-left: 50px; }
    }
  </style>
</head>
<body>
<div class="page">
  <header>
    <h1>SkyGuard Cutdown Pro</h1>
    <div class="subtitle">Live status &amp; cutdown configuration</div>
    <div class="status-bar">
      <span class="pill-group">
        <span class="pill-label">Firmware</span>
        <span class="pill-value" id="fwVersion">vX.Y.Z</span>
      </span>
      <span class="pill-group">
        <span class="pill-label">Device</span>
        <span class="pill-value" id="deviceName">SGCP-XXXX</span>
      </span>
    </div>
  </header>

  <!-- MAIN SETTINGS FORM -->
  <form method="POST" action="/save">
    <main>
      <!-- LIVE VIEW -->
      <section class="section">
        <h2>
          Live View
          <span class="tag">Read-only flight &amp; system state</span>
        </h2>
        <p class="help">
          Current values from the instrument. On the real device these fields update in real time;
          this mockup is for layout review only.
        </p>
        <div class="live-grid">
          <div class="live-item"><div class="live-label">Flight state</div><div class="live-value">GROUND / IN_FLIGHT / TERMINATED</div></div>
          <div class="live-item"><div class="live-label">Time since power on (s)</div><div class="live-value">1234</div></div>
          <div class="live-item"><div class="live-label">Time since launch (s)</div><div class="live-value">0 (not launched)</div></div>
          <div class="live-item"><div class="live-label">GPS fix quality</div><div class="live-value">0 (no fix) / 1+ (fix)</div></div>
          <div class="live-item"><div class="live-label">Latitude (deg)</div><div class="live-value">36.0000</div></div>
          <div class="live-item"><div class="live-label">Longitude (deg)</div><div class="live-value">-94.0000</div></div>
          <div class="live-item"><div class="live-label">GPS altitude (m)</div><div class="live-value">12345</div></div>
          <div class="live-item"><div class="live-label">Pressure (hPa)</div><div class="live-value">850.0</div></div>
          <div class="live-item"><div class="live-label">Temperature (°C)</div><div class="live-value">-20.5</div></div>
          <div class="live-item"><div class="live-label">Humidity (%)</div><div class="live-value">15.0</div></div>
          <div class="live-item"><div class="live-label">Next Iridium transmit in (s)</div><div class="live-value">120</div></div>
          <div class="live-item"><div class="live-label">Last cut reason</div><div class="live-value">none / bucket_logic / external / iridium</div></div>
        </div>
      </section>

      <!-- DEVICE / WIFI -->
      <section class="section">
        <h2>
          Device &amp; WiFi
          <span class="tag">CONFIG AP identity</span>
        </h2>
        <p class="help">
          CONFIG access point is <strong>CONFIG-&lt;serial&gt;</strong> and uses the password below.
          Serial number is preserved across factory resets (hold button at boot).
        </p>
        <div class="section-grid">
          <div class="field">
            <label for="serial_number">Serial number <span class="muted">(0 = unassigned, max 7 digits)</span></label>
            <input type="number" id="serial_number" name="serial_number" min="0" max="9999999" step="1" value="0">
          </div>
          <div class="field">
            <label for="ap_password">CONFIG AP password <span class="muted">(min 8 chars)</span></label>
            <input type="password" id="ap_password" name="ap_password" value="l33mange0">
          </div>
        </div>
      </section>

      <!-- FIELDWATCH -->
      <section class="section">
        <h2>
          FieldWatch
          <span class="tag">Telemetry routing</span>
        </h2>
        <p class="help">
          FieldWatch routing identifiers used by telemetry packets.
        </p>
        <div class="section-grid">
          <div>
            <!-- Hidden 0 ensures a value is always posted -->
            <input type="hidden" name="fw_enabled" value="0">
            <label class="toggle">
              <input type="checkbox" name="fw_enabled" value="1">
              <span class="toggle-track"><span class="toggle-thumb"></span></span>
              <span class="toggle-label">Enable FieldWatch routing</span>
            </label>
          </div>
          <div class="field">
            <label for="fw_device_id">Device ID</label>
            <input type="text" id="fw_device_id" name="fw_device_id" value="">
          </div>
          <div class="field">
            <label for="fw_access_token">Access token</label>
            <input type="text" id="fw_access_token" name="fw_access_token" value="">
          </div>
        </div>
      </section>

      <!-- CUTDOWN LOGIC -->
      <section class="section">
        <h2>
          Cutdown Logic
          <span class="tag">Bucket A &amp; B</span>
        </h2>
        <p class="help">
          The cutdown fires when <strong>all enabled requirements</strong> in Bucket&nbsp;A are true and
          <strong>any enabled trigger</strong> in Bucket&nbsp;B becomes true. External inputs and Iridium
          commands can optionally force a cut immediately.
        </p>
        <div class="section-grid">
          <div>
            <input type="hidden" name="gc_require_launch" value="0">
            <label class="toggle">
              <input type="checkbox" name="gc_require_launch" value="1" checked>
              <span class="toggle-track"><span class="toggle-thumb"></span></span>
              <span class="toggle-label">Require launch detection before cut</span>
            </label>
            <div class="toggle-subtext">No cut until the instrument has detected ascent.</div>
          </div>
          <div>
            <input type="hidden" name="gc_require_fix" value="0">
            <label class="toggle">
              <input type="checkbox" name="gc_require_fix" value="1">
              <span class="toggle-track"><span class="toggle-thumb"></span></span>
              <span class="toggle-label">Require GPS fix before cut</span>
            </label>
            <div class="toggle-subtext">All cut logic is blocked until GPS fix quality ≥ 1.</div>
          </div>
        </div>
      </section>

      <!-- BUCKET A -->
      <section class="section">
        <h2>Bucket A – Global Requirements (ALL must be true)</h2>
        <p class="help">
          Use Bucket A for conditions that must all be satisfied before any cut can occur
          (e.g., altitude within a box, geofence bounds, or time constraints).
        </p>

        <table class="conditions-table">
          <thead>
          <tr>
            <th>#</th>
            <th>Enabled</th>
            <th>Variable</th>
            <th>Operator</th>
            <th>Value</th>
            <th>For at least (s)</th>
          </tr>
          </thead>
          <tbody>
          <!-- 10 rows for Bucket A -->

          <!-- Row 1 (index 0) -->
          <tr>
            <td class="cond-index">1</td>
            <td class="cond-enabled">
              <input type="hidden" name="a0_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="a0_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td>
              <select name="a0_var">
                <option value="t_power_s">Time since power on (s)</option>
                <option value="t_launch_s">Time since launch (s)</option>
                <option value="gps_alt_m">GPS altitude (m)</option>
                <option value="gps_lat_deg">Latitude (deg)</option>
                <option value="gps_lon_deg">Longitude (deg)</option>
                <option value="gps_fix">GPS fix quality</option>
                <option value="pressure_hPa">Pressure (hPa)</option>
                <option value="temp_C">Temperature (°C)</option>
                <option value="humidity_pct">Humidity (%)</option>
              </select>
            </td>
            <td>
              <select name="a0_op">
                <option value="gt">&gt;</option>
                <option value="gte">&gt;=</option>
                <option value="eq">=</option>
                <option value="lte">&lt;=</option>
                <option value="lt">&lt;</option>
              </select>
            </td>
            <td><input type="number" name="a0_value" step="any"></td>
            <td><input type="number" name="a0_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 2 (index 1) -->
          <tr>
            <td class="cond-index">2</td>
            <td class="cond-enabled">
              <input type="hidden" name="a1_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="a1_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td>
              <select name="a1_var">
                <option value="t_power_s">Time since power on (s)</option>
                <option value="t_launch_s">Time since launch (s)</option>
                <option value="gps_alt_m">GPS altitude (m)</option>
                <option value="gps_lat_deg">Latitude (deg)</option>
                <option value="gps_lon_deg">Longitude (deg)</option>
                <option value="gps_fix">GPS fix quality</option>
                <option value="pressure_hPa">Pressure (hPa)</option>
                <option value="temp_C">Temperature (°C)</option>
                <option value="humidity_pct">Humidity (%)</option>
              </select>
            </td>
            <td>
              <select name="a1_op">
                <option value="gt">&gt;</option>
                <option value="gte">&gt;=</option>
                <option value="eq">=</option>
                <option value="lte">&lt;=</option>
                <option value="lt">&lt;</option>
              </select>
            </td>
            <td><input type="number" name="a1_value" step="any"></td>
            <td><input type="number" name="a1_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 3 (index 2) -->
          <tr>
            <td class="cond-index">3</td>
            <td class="cond-enabled">
              <input type="hidden" name="a2_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="a2_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="a2_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="a2_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="a2_value" step="any"></td>
            <td><input type="number" name="a2_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 4 (index 3) -->
          <tr>
            <td class="cond-index">4</td>
            <td class="cond-enabled">
              <input type="hidden" name="a3_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="a3_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="a3_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="a3_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="a3_value" step="any"></td>
            <td><input type="number" name="a3_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 5 (index 4) -->
          <tr>
            <td class="cond-index">5</td>
            <td class="cond-enabled">
              <input type="hidden" name="a4_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="a4_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="a4_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="a4_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="a4_value" step="any"></td>
            <td><input type="number" name="a4_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 6 (index 5) -->
          <tr>
            <td class="cond-index">6</td>
            <td class="cond-enabled">
              <input type="hidden" name="a5_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="a5_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="a5_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="a5_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="a5_value" step="any"></td>
            <td><input type="number" name="a5_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 7 (index 6) -->
          <tr>
            <td class="cond-index">7</td>
            <td class="cond-enabled">
              <input type="hidden" name="a6_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="a6_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="a6_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="a6_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="a6_value" step="any"></td>
            <td><input type="number" name="a6_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 8 (index 7) -->
          <tr>
            <td class="cond-index">8</td>
            <td class="cond-enabled">
              <input type="hidden" name="a7_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="a7_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="a7_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="a7_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="a7_value" step="any"></td>
            <td><input type="number" name="a7_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 9 (index 8) -->
          <tr>
            <td class="cond-index">9</td>
            <td class="cond-enabled">
              <input type="hidden" name="a8_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="a8_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="a8_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="a8_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="a8_value" step="any"></td>
            <td><input type="number" name="a8_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 10 (index 9) -->
          <tr>
            <td class="cond-index">10</td>
            <td class="cond-enabled">
              <input type="hidden" name="a9_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="a9_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="a9_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="a9_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="a9_value" step="any"></td>
            <td><input type="number" name="a9_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          </tbody>
        </table>
        <div class="conditions-note">
          Example: geofence using four rows (lat&nbsp;min, lat&nbsp;max, lon&nbsp;min, lon&nbsp;max).
        </div>
      </section>

      <!-- BUCKET B -->
      <section class="section">
        <h2>Bucket B – Cut Triggers (ANY can trigger)</h2>
        <p class="help">
          Bucket B contains the conditions that actually initiate the cut once all Bucket A
          requirements are met (e.g., maximum time aloft, altitude ceiling, or pressure limits).
        </p>

        <table class="conditions-table">
          <thead>
          <tr>
            <th>#</th>
            <th>Enabled</th>
            <th>Variable</th>
            <th>Operator</th>
            <th>Value</th>
            <th>For at least (s)</th>
          </tr>
          </thead>
          <tbody>
          <!-- Row 1 (index 0) -->
          <tr>
            <td class="cond-index">1</td>
            <td class="cond-enabled">
              <input type="hidden" name="b0_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="b0_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="b0_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="b0_op">
              <option value="gt">&gt;</option><option value="gte" selected>&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="b0_value" step="any" placeholder="e.g. 30000"></td>
            <td><input type="number" name="b0_for_s" min="0" step="1" placeholder="10"></td>
          </tr>

          <!-- Rows 2–10 (indices 1..9) -->
          <!-- Row 2 -->
          <tr>
            <td class="cond-index">2</td>
            <td class="cond-enabled">
              <input type="hidden" name="b1_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="b1_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="b1_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="b1_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="b1_value" step="any"></td>
            <td><input type="number" name="b1_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 3 -->
          <tr>
            <td class="cond-index">3</td>
            <td class="cond-enabled">
              <input type="hidden" name="b2_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="b2_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="b2_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="b2_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="b2_value" step="any"></td>
            <td><input type="number" name="b2_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 4 -->
          <tr>
            <td class="cond-index">4</td>
            <td class="cond-enabled">
              <input type="hidden" name="b3_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="b3_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="b3_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="b3_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="b3_value" step="any"></td>
            <td><input type="number" name="b3_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 5 -->
          <tr>
            <td class="cond-index">5</td>
            <td class="cond-enabled">
              <input type="hidden" name="b4_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="b4_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="b4_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="b4_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="b4_value" step="any"></td>
            <td><input type="number" name="b4_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 6 -->
          <tr>
            <td class="cond-index">6</td>
            <td class="cond-enabled">
              <input type="hidden" name="b5_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="b5_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="b5_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="b5_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="b5_value" step="any"></td>
            <td><input type="number" name="b5_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 7 -->
          <tr>
            <td class="cond-index">7</td>
            <td class="cond-enabled">
              <input type="hidden" name="b6_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="b6_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="b6_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="b6_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="b6_value" step="any"></td>
            <td><input type="number" name="b6_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 8 -->
          <tr>
            <td class="cond-index">8</td>
            <td class="cond-enabled">
              <input type="hidden" name="b7_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="b7_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="b7_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="b7_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="b7_value" step="any"></td>
            <td><input type="number" name="b7_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 9 -->
          <tr>
            <td class="cond-index">9</td>
            <td class="cond-enabled">
              <input type="hidden" name="b8_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="b8_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="b8_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="b8_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="b8_value" step="any"></td>
            <td><input type="number" name="b8_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          <!-- Row 10 -->
          <tr>
            <td class="cond-index">10</td>
            <td class="cond-enabled">
              <input type="hidden" name="b9_enabled" value="0">
              <label class="toggle">
                <input type="checkbox" name="b9_enabled" value="1">
                <span class="toggle-track"><span class="toggle-thumb"></span></span>
              </label>
            </td>
            <td><select name="b9_var">
              <option value="t_power_s">Time since power on (s)</option>
              <option value="t_launch_s">Time since launch (s)</option>
              <option value="gps_alt_m">GPS altitude (m)</option>
              <option value="gps_lat_deg">Latitude (deg)</option>
              <option value="gps_lon_deg">Longitude (deg)</option>
              <option value="gps_fix">GPS fix quality</option>
              <option value="pressure_hPa">Pressure (hPa)</option>
              <option value="temp_C">Temperature (°C)</option>
              <option value="humidity_pct">Humidity (%)</option>
            </select></td>
            <td><select name="b9_op">
              <option value="gt">&gt;</option><option value="gte">&gt;=</option><option value="eq">=</option>
              <option value="lte">&lt;=</option><option value="lt">&lt;</option>
            </select></td>
            <td><input type="number" name="b9_value" step="any"></td>
            <td><input type="number" name="b9_for_s" min="0" step="1" placeholder="0"></td>
          </tr>

          </tbody>
        </table>

        <div class="conditions-note">
          Example: “Cut if GPS altitude ≥ 30000&nbsp;m for 10&nbsp;s” or “Cut if pressure ≤ threshold”.
        </div>
      </section>

      <!-- EXTERNAL INPUTS -->
      <section class="section">
        <h2>External Cut Inputs</h2>
        <p class="help">
          External optoisolated inputs can force an immediate cut when active, regardless of Bucket A/B logic.
        </p>

        <div class="section-grid">
          <div>
            <input type="hidden" name="ext0_enabled" value="0">
            <label class="toggle">
              <input type="checkbox" name="ext0_enabled" value="1" checked>
              <span class="toggle-track"><span class="toggle-thumb"></span></span>
              <span class="toggle-label">External Input 1 enabled</span>
            </label>

            <div class="field">
              <label for="ext0_active_high">Active level</label>
              <select id="ext0_active_high" name="ext0_active_high">
                <option value="1">Active high</option>
                <option value="0">Active low</option>
              </select>
            </div>

            <div class="field">
              <label for="ext0_debounce_ms">Debounce <span class="muted">(ms)</span></label>
              <input type="number" id="ext0_debounce_ms" name="ext0_debounce_ms" min="0" step="1" value="50">
            </div>
          </div>

          <div>
            <input type="hidden" name="ext1_enabled" value="0">
            <label class="toggle">
              <input type="checkbox" name="ext1_enabled" value="1">
              <span class="toggle-track"><span class="toggle-thumb"></span></span>
              <span class="toggle-label">External Input 2 enabled</span>
            </label>

            <div class="field">
              <label for="ext1_active_high">Active level</label>
              <select id="ext1_active_high" name="ext1_active_high">
                <option value="1">Active high</option>
                <option value="0">Active low</option>
              </select>
            </div>

            <div class="field">
              <label for="ext1_debounce_ms">Debounce <span class="muted">(ms)</span></label>
              <input type="number" id="ext1_debounce_ms" name="ext1_debounce_ms" min="0" step="1" value="50">
            </div>
          </div>
        </div>
      </section>

      <!-- IRIDIUM -->
      <section class="section">
        <h2>Iridium Telemetry &amp; Remote Cut</h2>
        <p class="help">
          Configure progressive telemetry rates and optional remote cut commands via Iridium messages.
        </p>

        <div class="section-grid">
          <div>
            <input type="hidden" name="ir_enabled" value="0">
            <label class="toggle">
              <input type="checkbox" name="ir_enabled" value="1" checked>
              <span class="toggle-track"><span class="toggle-thumb"></span></span>
              <span class="toggle-label">Iridium telemetry enabled</span>
            </label>

            <input type="hidden" name="ir_remote_cut" value="0">
            <label class="toggle" style="margin-top:6px;">
              <input type="checkbox" name="ir_remote_cut" value="1" checked>
              <span class="toggle-track"><span class="toggle-thumb"></span></span>
              <span class="toggle-label">Allow remote Iridium cut command</span>
            </label>

            <div class="field" style="margin-top:6px;">
              <label for="ir_token">Remote cut token</label>
              <input type="text" id="ir_token" name="ir_token" value="CUTDOWN">
              <span class="muted">Token must appear in downlink text to trigger cut.</span>
            </div>
          </div>

          <div>
            <div class="field">
              <label for="ir_ascent_s">Ascent interval <span class="muted">(s between packets before cut)</span></label>
              <input type="number" id="ir_ascent_s" name="ir_ascent_s" min="0" step="1" value="600">
            </div>

            <div class="field">
              <label for="ir_descent_s">Descent interval <span class="muted">(s between packets after cut)</span></label>
              <input type="number" id="ir_descent_s" name="ir_descent_s" min="0" step="1" value="60">
            </div>

            <div class="field">
              <label for="ir_descent_dur_s">Descent window duration <span class="muted">(s after cut)</span></label>
              <input type="number" id="ir_descent_dur_s" name="ir_descent_dur_s" min="0" step="1" value="1800">
            </div>

            <div class="field">
              <label for="ir_beacon_s">Beacon interval <span class="muted">(s after descent window)</span></label>
              <input type="number" id="ir_beacon_s" name="ir_beacon_s" min="0" step="1" value="3600">
            </div>
          </div>
        </div>
      </section>

      <!-- RELEASE MECHANISM -->
      <section class="section">
        <h2>Release Mechanism</h2>
        <p class="help">
          The release mechanism is wiggled on boot, then moves to <strong>lock</strong>. When a cut is commanded,
          it moves once to <strong>release</strong> and remains there until power is removed.
        </p>

        <div class="field-inline">
          <!-- These are separate POST targets; firmware will implement handlers -->
          <button type="submit" class="btn" formaction="/lock" formmethod="POST">Lock</button>
          <button type="submit" class="btn btn-danger" formaction="/release" formmethod="POST">Release</button>
        </div>
      </section>

      <!-- SAVE / LOAD -->
      <section class="section">
        <div class="button-row">
          <button type="submit" class="btn" formaction="/defaults" formmethod="POST">Restore defaults</button>
          <button type="submit" class="btn btn-primary" formaction="/save" formmethod="POST">Save settings</button>
        </div>
      </section>
    </main>
  </form>
</div>
</body>
</html>
)rawliteral";

/**
 * @brief OTA firmware upload page (simple, functional).
 *
 * This is intentionally minimal. You can replace with your own styled page later.
 */
static const char FIRMWARE_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>SkyGuard Cutdown Pro – Firmware Update</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
</head>
<body style="font-family: sans-serif; padding: 20px;">
  <h1>Firmware Update</h1>
  <p>Select a <code>.bin</code> file built for this hardware and upload it.</p>
  <form method="POST" action="/firmware" enctype="multipart/form-data">
    <input type="file" name="update">
    <button type="submit">Upload</button>
  </form>
  <p><a href="/">Back to Settings</a></p>
</body>
</html>
)rawliteral";

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
            case '\\\\': out += F("\\\\\\\\"); break;
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
    js += F("setValById('serial_number','");
    js += String((unsigned long)cfg.device.serial_number);
    js += F("');\n");
    js += F("setPassword('ap_password','");
    js += jsEscapeSingleQuoted(cfg.device.ap_password);
    js += F("');\n");

    // FieldWatch
    js += F("setCheck('fw_enabled',");
    js += (cfg.fieldwatch.enabled ? "1" : "0");
    js += F(");\n");
    js += F("setText('fw_device_id','");
    js += jsEscapeSingleQuoted(cfg.fieldwatch.device_id);
    js += F("');\n");
    js += F("setText('fw_access_token','");
    js += jsEscapeSingleQuoted(cfg.fieldwatch.access_token);
    js += F("');\n");

    // Global cut toggles
    js += F("setCheck('gc_require_launch',");
    js += (cfg.global_cutdown.require_launch_before_cut ? "1" : "0");
    js += F(");\n");
    js += F("setCheck('gc_require_fix',");
    js += (cfg.global_cutdown.require_gps_fix_before_cut ? "1" : "0");
    js += F(");\n");

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
        if (candidate.iridium.descent_interval_s < 10) addError("Iridium descent interval must be >= 10 s");
        if (candidate.iridium.beacon_interval_s < 10) addError("Iridium beacon interval must be >= 10 s");
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

    // Device serial + AP password
    if (g_server->hasArg("serial_number")) {
        candidate.device.serial_number = toU32(g_server->arg("serial_number"), candidate.device.serial_number);
    }
    if (g_server->hasArg("ap_password")) {
        copyArgToBuf(candidate.device.ap_password, sizeof(candidate.device.ap_password), g_server->arg("ap_password"));
    }

    // Global cut toggles
    if (g_server->hasArg("gc_require_launch")) {
        candidate.global_cutdown.require_launch_before_cut =
            toBool(g_server->arg("gc_require_launch"), candidate.global_cutdown.require_launch_before_cut);
    }
    if (g_server->hasArg("gc_require_fix")) {
        candidate.global_cutdown.require_gps_fix_before_cut =
            toBool(g_server->arg("gc_require_fix"), candidate.global_cutdown.require_gps_fix_before_cut);
    }

    // FieldWatch
    if (g_server->hasArg("fw_enabled")) {
        candidate.fieldwatch.enabled = toBool(g_server->arg("fw_enabled"), candidate.fieldwatch.enabled);
    }
    if (g_server->hasArg("fw_device_id")) {
        copyArgToBuf(candidate.fieldwatch.device_id, sizeof(candidate.fieldwatch.device_id), g_server->arg("fw_device_id"));
    }
    if (g_server->hasArg("fw_access_token")) {
        copyArgToBuf(candidate.fieldwatch.access_token, sizeof(candidate.fieldwatch.access_token), g_server->arg("fw_access_token"));
    }

    // External inputs
    // ext0_enabled/ext0_active_high/ext0_debounce_ms, ext1_...
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
    server.on("/", HTTP_GET, [&]() {
        sendSettingsPage(nullptr, false);
    });

    server.on("/save", HTTP_POST, [&]() {
        handleSave();
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
