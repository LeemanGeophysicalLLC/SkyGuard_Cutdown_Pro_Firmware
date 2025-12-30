#pragma once

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
        <div class="section-grid"><div class="field">
            <label for="ap_password">CONFIG AP password <span class="muted">(min 8 chars)</span></label>
            <input type="password" id="ap_password" name="ap_password" value="l33mange0">
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
              <label for="ext0_active_high">Signal level</label>
              <select id="ext0_active_high" name="ext0_active_high">
                <option value="1">Cut when high</option>
                <option value="0">Cut when low</option>
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
              <label for="ext1_active_high">Signal level</label>
              <select id="ext1_active_high" name="ext1_active_high">
                <option value="1">Cut when high</option>
                <option value="0">Cut when low</option>
              </select>
            </div>

            <div class="field">
              <label for="ext1_debounce_ms">Debounce <span class="muted">(ms)</span></label>
              <input type="number" id="ext1_debounce_ms" name="ext1_debounce_ms" min="0" step="1" value="50">
            </div>
          </div>
        </div>
      </section>


      <!-- TERMINATION DETECTION -->
      <section class="section">
        <h2>
          Termination Detection
          <span class="tag">Detect descent without cut</span>
        </h2>
        <p class="help">
          If enabled, the instrument declares <strong>terminated</strong> when it detects sustained descent after launch
          (useful when the balloon pops without a commanded cut). This affects phase-dependent behavior (like Iridium cadence).
        </p>

        <div class="section-grid">
          <div>
            <input type="hidden" name="term_enabled" value="0">
            <label class="toggle">
              <input type="checkbox" name="term_enabled" value="1" checked>
              <span class="toggle-track"><span class="toggle-thumb"></span></span>
              <span class="toggle-label">Enable termination detection</span>
            </label>

            <div class="field" style="margin-top:6px;">
              <label for="term_sustain_s">Sustain time <span class="muted">(s, consecutive seconds)</span></label>
              <input type="number" id="term_sustain_s" name="term_sustain_s" min="1" step="1" value="15">
            </div>
          </div>

          <div>
            <input type="hidden" name="term_use_gps" value="0">
            <label class="toggle">
              <input type="checkbox" name="term_use_gps" value="1" checked>
              <span class="toggle-track"><span class="toggle-thumb"></span></span>
              <span class="toggle-label">Use GPS peak-drop detector</span>
            </label>
            <div class="toggle-subtext">Triggers if GPS altitude drops below the in-flight peak.</div>

            <div class="field" style="margin-top:6px;">
              <label for="term_gps_drop_m">GPS drop threshold <span class="muted">(m below peak)</span></label>
              <input type="number" id="term_gps_drop_m" name="term_gps_drop_m" min="0" step="0.1" value="60.0">
            </div>
          </div>

          <div>
            <input type="hidden" name="term_use_pressure" value="0">
            <label class="toggle">
              <input type="checkbox" name="term_use_pressure" value="1" checked>
              <span class="toggle-track"><span class="toggle-thumb"></span></span>
              <span class="toggle-label">Use pressure min-rise detector</span>
            </label>
            <div class="toggle-subtext">Triggers if pressure rises above the in-flight minimum.</div>

            <div class="field" style="margin-top:6px;">
              <label for="term_pressure_rise_hpa">Pressure rise threshold <span class="muted">(hPa above min)</span></label>
              <input type="number" id="term_pressure_rise_hpa" name="term_pressure_rise_hpa" min="0" step="0.1" value="50.0">
            </div>
          </div>
        </div>
      </section>

      <!-- IRIDIUM -->
      <section class="section">
        <h2>Iridium Telemetry &amp; Remote Cut</h2>
        <p class="help">
          Configure progressive telemetry rates and optional remote cut commands via Iridium messages. Mailbox reception is performed during transmit sessions (no separate polling interval).
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

          <div>            <div class="field">
              <label for="ir_ground_s">Ground interval <span class="muted">(s between packets pre-launch, 0 = disabled)</span></label>
              <input type="number" id="ir_ground_s" name="ir_ground_s" min="0" step="1" value="0">
            </div>


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
