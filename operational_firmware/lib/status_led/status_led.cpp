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
static uint16_t s_pulse_width_ms = STATUS_LED_PULSE_WIDTH_MS;

// Pulse timing inside each 1-second frame
static constexpr uint16_t PULSE_WIDTH_MS  = STATUS_LED_PULSE_WIDTH_MS;
static constexpr uint16_t PULSE_PERIOD_MS = STATUS_LED_PULSE_PERIOD_MS;
static constexpr uint16_t WARNING_PULSE_WIDTH_MS = 250;
static constexpr uint16_t LOCATOR_PULSE_WIDTH_MS = 250;
static constexpr uint16_t BOOT_COLOR_HOLD_MS = 1000;
static constexpr uint16_t BOOT_OFF_HOLD_MS = 1000;
static constexpr uint16_t BOOT_BREATHE_CYCLE_MS = 2000;
static constexpr uint16_t BOOT_BREATHE_STEP_MS = 25;

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
    s_pulse_width_ms = PULSE_WIDTH_MS;
}

void statusLedShowBootSequence() {
    setPixel(255, 0, 0);
    delay(BOOT_COLOR_HOLD_MS);

    setPixel(0, 255, 0);
    delay(BOOT_COLOR_HOLD_MS);

    setPixel(0, 0, 255);
    delay(BOOT_COLOR_HOLD_MS);
    
    setPixel(0, 0, 0);
    delay(BOOT_OFF_HOLD_MS);
}

void statusLedShowBootInProgress() {
    setPixel(255, 160, 0);
}

void statusLedShowBootChargeFrame(uint32_t elapsed_ms) {
    const uint16_t phase_ms = (uint16_t)(elapsed_ms % BOOT_BREATHE_CYCLE_MS);

    uint16_t ramp = phase_ms;
    if (ramp >= (BOOT_BREATHE_CYCLE_MS / 2)) {
        ramp = (uint16_t)(BOOT_BREATHE_CYCLE_MS - ramp);
    }

    const uint16_t max_r = 255;
    const uint16_t max_g = 160;
    const uint16_t min_r = 8;
    const uint16_t min_g = 5;
    const uint16_t half_cycle = BOOT_BREATHE_CYCLE_MS / 2;

    const uint8_t r = (uint8_t)(min_r + ((max_r - min_r) * ramp) / half_cycle);
    const uint8_t g = (uint8_t)(min_g + ((max_g - min_g) * ramp) / half_cycle);

    setPixel(r, g, 0);
}

void statusLedShowBootChargeBreathe(uint32_t duration_ms) {
    const uint32_t start_ms = millis();

    while ((millis() - start_ms) < duration_ms) {
        const uint32_t elapsed_ms = millis() - start_ms;
        statusLedShowBootChargeFrame(elapsed_ms);
        delay(BOOT_BREATHE_STEP_MS);
    }

    setPixel(255, 160, 0);
}

void statusLedUpdate1Hz(uint32_t now_ms) {
    (void)now_ms;
    s_pulse_width_ms = PULSE_WIDTH_MS;

    // Priority 1: CRIT -> 3 red pulses
    if (errorsGetOverallSeverity() == ERROR_SEV_CRIT) {
        s_solid = false;
        s_pulses_per_sec = STATUS_LED_PULSES_RED;
        s_r = 255; s_g = 0; s_b = 0;
        return;
    }

    // Priority 2: terminated -> white pulse locator beacon
    if (g_state.terminated) {
        s_solid = false;
        s_pulses_per_sec = 1;
        s_pulse_width_ms = LOCATOR_PULSE_WIDTH_MS;
        s_r = 255; s_g = 255; s_b = 255;
        return;
    }

    // Priority 3: config mode -> solid blue
    if (g_state.system_mode == MODE_CONFIG) {
        s_solid = true;
        s_pulses_per_sec = 0;
        s_r = 0; s_g = 0; s_b = 255;
        return;
    }

    // Priority 4: launch not advised warnings -> yellow pulses
    const bool gps_dead = errorIsActive(ERR_GPS);
    const bool waiting_for_fix = (!g_state.launch_detected) && (!gps_dead) && (!g_readings.gps_fix);
    if (waiting_for_fix || errorsGetOverallSeverity() == ERROR_SEV_WARN) {
        s_solid = false;
        s_pulses_per_sec = 1;
        s_pulse_width_ms = WARNING_PULSE_WIDTH_MS;
        s_r = 255; s_g = 160; s_b = 0;
        return;
    }

    // Priority 5: on the ground and ready -> solid green
    if (!g_state.launch_detected) {
        s_solid = true;
        s_pulses_per_sec = 0;
        s_r = 0; s_g = 255; s_b = 0;
        return;
    }

    // Priority 6: in-flight -> green pulse to save power
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
        const uint16_t end   = (uint16_t)(start + s_pulse_width_ms);
        if (t >= start && t < end) {
            on = true;
            break;
        }
    }

    if (on) setPixel(s_r, s_g, s_b);
    else    setPixel(0, 0, 0);
}
