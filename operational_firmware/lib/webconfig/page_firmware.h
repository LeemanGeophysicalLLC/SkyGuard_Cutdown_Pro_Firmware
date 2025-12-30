#pragma once

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
