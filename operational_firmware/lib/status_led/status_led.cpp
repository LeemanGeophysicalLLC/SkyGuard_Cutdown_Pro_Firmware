#include "status_led.h"

#include <Adafruit_NeoPixel.h>

#include "pins.h"
#include "errors.h"
#include "readings.h"
#include "state.h"
#include "project_config.h"

static Adafruit_NeoPixel s_px(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// Render state chosen at 1 Hz
static bool    s_solid = false;
static uint8_t s_pulses_per_sec = 0;
static uint8_t s_r = 0, s_g = 0, s_b = 0;

// Pulse timing inside each 1-second frame
static constexpr uint16_t PULSE_WIDTH_MS  = STATUS_LED_PULSE_WIDTH_MS;
static constexpr uint16_t PULSE_PERIOD_MS = STATUS_LED_PULSE_PERIOD_MS;

static void setPixel(uint8_t r, uint8_t g, uint8_t b) {
    s_px.setPixelColor(0, s_px.Color(r, g, b));
    s_px.show();
}

void statusLedInit() {
    s_px.begin();
    s_px.setBrightness(STATUS_LED_BRIGHTNESS);   // adjust if needed
    setPixel(0, 0, 0);

    s_solid = false;
    s_pulses_per_sec = 0;
    s_r = s_g = s_b = 0;
}

void statusLedUpdate1Hz(uint32_t now_ms) {
    (void)now_ms;

    // Priority 1: CRIT -> 3 red pulses
    if (errorsGetOverallSeverity() == ERROR_SEV_CRIT) {
        s_solid = false;
        s_pulses_per_sec = STATUS_LED_PULSES_RED;
        s_r = 255; s_g = 0; s_b = 0;
        return;
    }

    // Priority 2: GPS warmup (pre-launch, GPS talking, no fix yet) -> solid blue
    const bool gps_dead = errorIsActive(ERR_GPS);             // ERR_GPS == comms dead
    const bool waiting_for_fix = (!g_state.launch_detected) && (!gps_dead) && (!g_readings.gps_fix);
    if (waiting_for_fix) {
        s_solid = true;
        s_pulses_per_sec = 0;
        s_r = 0; s_g = 0; s_b = 255;
        return;
    }

    // Priority 3: WARN -> 2 yellow pulses
    if (errorsGetOverallSeverity() == ERROR_SEV_WARN) {
        s_solid = false;
        s_pulses_per_sec = STATUS_LED_PULSES_YELLOW;
        s_r = 255; s_g = 160; s_b = 0;
        return;
    }

    // Priority 4: OK -> 1 green pulse
    s_solid = false;
    s_pulses_per_sec = STATUS_LED_PULSES_GREEN;
    s_r = 0; s_g = 255; s_b = 0;
}

void statusLedUpdateFast(uint32_t now_ms) {
    const uint16_t t = (uint16_t)(now_ms % 1000U);

    if (s_solid) {
        setPixel(s_r, s_g, s_b);
        return;
    }

    bool on = false;
    for (uint8_t i = 0; i < s_pulses_per_sec; i++) {
        const uint16_t start = (uint16_t)(i * PULSE_PERIOD_MS);
        const uint16_t end   = (uint16_t)(start + PULSE_WIDTH_MS);
        if (t >= start && t < end) {
            on = true;
            break;
        }
    }

    if (on) setPixel(s_r, s_g, s_b);
    else    setPixel(0, 0, 0);
}
