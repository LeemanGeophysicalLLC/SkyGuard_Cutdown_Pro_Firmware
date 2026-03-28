#pragma once
#include <Arduino.h>

void statusLedInit();

// Power-on self-test: red, green, blue sequence.
void statusLedShowBootSequence();

// Show solid yellow while boot/setup is still in progress.
void statusLedShowBootInProgress();

// Breathe yellow for a fixed time while the supercap charges during boot.
void statusLedShowBootChargeBreathe(uint32_t duration_ms);

// Render one frame of the yellow boot-charge breathing animation.
void statusLedShowBootChargeFrame(uint32_t elapsed_ms);

// Call once per second (your 1 Hz tick): decide desired mode/pattern.
void statusLedUpdate1Hz(uint32_t now_ms);

// Call as fast as possible in loop(): renders short pulses within the 1-second frame.
void statusLedUpdateFast(uint32_t now_ms);
