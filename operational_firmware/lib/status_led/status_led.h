#pragma once
#include <Arduino.h>

void statusLedInit();

// Call once per second (your 1 Hz tick): decide desired mode/pattern.
void statusLedUpdate1Hz(uint32_t now_ms);

// Call as fast as possible in loop(): renders short pulses within the 1-second frame.
void statusLedUpdateFast(uint32_t now_ms);
