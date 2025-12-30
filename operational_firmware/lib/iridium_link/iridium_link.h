#pragma once
#include <Arduino.h>

void iridiumInit();
void iridiumUpdate1Hz(uint32_t now_ms);

// One-shot latch: returns true once when a valid remote cut is received, then clears.
bool iridiumGetRemoteCutRequestAndClear();

// True while an Iridium SBD session is in progress (useful for status LED / scheduling).
bool iridiumIsBusy();
