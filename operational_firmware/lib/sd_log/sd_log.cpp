#include "sd_log.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include "pins.h"
#include "project_config.h"
#include "errors.h"
#include "readings.h"
#include "state.h"
#include "debug.h"

// Use VSPI on ESP32 (default for SPIClass(VSPI))
static SPIClass s_sdSPI(VSPI);

static bool s_sd_present = false;
static bool s_sd_mounted = false;
static char s_filename[16] = {0}; // "00000001.TXT" + null
static bool s_header_written = false;

static bool sdCardPresent() {
    pinMode(PIN_SD_CD, INPUT_PULLUP);

    const int level = digitalRead(PIN_SD_CD);
    if (SD_CD_ACTIVE_LOW) return (level == LOW);
    return (level == HIGH);
}

static bool mountSD() {
    // Ensure SPI pins are configured (explicit = deterministic)
    s_sdSPI.begin(PIN_SPI_CLK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SD_CS);

    if (!SD.begin(PIN_SD_CS, s_sdSPI, SD_SPI_CLOCK_HZ)) {
        return false;
    }
    return true;
}

static uint32_t findNextLogIndex() {
    // Scan root for ########.TXT and take max+1.
    // If scan fails or no files, return 1.
    uint32_t max_idx = 0;

    File root = SD.open(SD_LOG_DIR);
    if (!root) return 1;

    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            const char* name = f.name();  // may include path on some cores; usually "00000001.TXT"
            // Find last path segment
            const char* base = strrchr(name, '/');
            base = (base) ? (base + 1) : name;

            // Expect exactly 8 digits + ".TXT"
            if (strlen(base) == 12 &&
                base[8] == '.' &&
                (base[9] == 'T' || base[9] == 't') &&
                (base[10] == 'X' || base[10] == 'x') &&
                (base[11] == 'T' || base[11] == 't')) {

                bool all_digits = true;
                uint32_t val = 0;
                for (int i = 0; i < 8; i++) {
                    if (base[i] < '0' || base[i] > '9') { all_digits = false; break; }
                    val = val * 10 + (uint32_t)(base[i] - '0');
                }
                if (all_digits && val > max_idx) {
                    max_idx = val;
                }
            }
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();

    if (max_idx >= 99999999UL) {
        // Wrapped: start over (or choose to clamp). We'll clamp to max.
        return 99999999UL;
    }
    return max_idx + 1;
}

static bool openAndWriteHeaderIfNeeded() {
    if (s_header_written) return true;

    File file = SD.open(s_filename, FILE_WRITE);
    if (!file) return false;

    // Tab-separated header
    file.println(
        "t_power_s\t"
        "launch_detected\t"
        "cut_fired\t"
        "cut_reason\t"
        "gps_fix\tlat_deg\tlon_deg\talt_m\t"
        "temp_c\tpressure_hpa\thumidity_pct"
    );
    file.close();

    s_header_written = true;
    return true;
}

bool sdLogIsReady() {
    return s_sd_present && s_sd_mounted && (s_filename[0] != '\0');
}

void sdLogInit() {
    s_sd_present = false;
    s_sd_mounted = false;
    s_filename[0] = '\0';
    s_header_written = false;

    // Presence first
    s_sd_present = sdCardPresent();
    if (!s_sd_present) {
        errorSet(ERR_SD_MISSING);
        errorClear(ERR_SD_IO);
        return;
    }

    errorClear(ERR_SD_MISSING);

    // Mount
    if (!mountSD()) {
        errorSet(ERR_SD_IO);
        s_sd_mounted = false;
        return;
    }

    errorClear(ERR_SD_IO);
    s_sd_mounted = true;

    // Choose file
    const uint32_t idx = findNextLogIndex();
    snprintf(s_filename, sizeof(s_filename), "%08lu.TXT", (unsigned long)idx);

    // Write header (once)
    if (!openAndWriteHeaderIfNeeded()) {
        errorSet(ERR_SD_IO);
        s_sd_mounted = false;
        return;
    }
}

void sdLogUpdate1Hz(uint32_t now_ms) {
    (void)now_ms;

    // Re-check presence each tick so insert/remove is handled.
    const bool present_now = sdCardPresent();
    if (!present_now) {
        // Card removed or missing
        s_sd_present = false;
        s_sd_mounted = false;
        s_filename[0] = '\0';
        s_header_written = false;

        errorSet(ERR_SD_MISSING);
        errorClear(ERR_SD_IO);
        return;
    }

    // Card present
    s_sd_present = true;
    errorClear(ERR_SD_MISSING);

    // If not mounted yet, try to mount + open a new file
    if (!s_sd_mounted || s_filename[0] == '\0') {
        sdLogInit();
        // If still not ready, stop here
        if (!sdLogIsReady()) return;
    }

    // Build one TSV line. Open->append->close every tick.
    char line[SD_LOG_LINE_MAX];

    // GPS fields (use validity flags)
    const float lat = g_readings.gps_lat_valid ? g_readings.gps_lat_deg : NAN;
    const float lon = g_readings.gps_lon_valid ? g_readings.gps_lon_deg : NAN;
    const float alt = g_readings.gps_alt_valid ? g_readings.gps_alt_m : NAN;

    const float temp = g_readings.temp_valid ? g_readings.temp_c : NAN;
    const float pres = g_readings.pressure_valid ? g_readings.pressure_hpa : NAN;
    const float rh   = g_readings.humidity_valid ? g_readings.humidity_pct : NAN;

    // Cut latch + reason
    const int cut_fired = g_state.cut_fired ? 1 : 0;

    // Assumption: state has a numeric cut_reason (enum/int). Adjust name if needed.
    const int cut_reason = (int)g_state.cut_reason;

    snprintf(line, sizeof(line),
        "%lu\t%d\t%d\t%d\t%d\t%.7f\t%.7f\t%.1f\t%.2f\t%.2f\t%.2f",
        (unsigned long)g_state.t_power_s,
        g_state.launch_detected ? 1 : 0,
        cut_fired,
        cut_reason,
        (g_readings.gps_fix_valid && g_readings.gps_fix) ? 1 : 0,
        (double)lat, (double)lon, (double)alt,
        (double)temp, (double)pres, (double)rh
    );

    File file = SD.open(s_filename, FILE_APPEND);
    if (!file) {
        errorSet(ERR_SD_IO);
        s_sd_mounted = false;
        return;
    }

    if (!file.println(line)) {
        errorSet(ERR_SD_IO);
        file.close();
        s_sd_mounted = false;
        return;
    }

    file.close();
    errorClear(ERR_SD_IO);
}
