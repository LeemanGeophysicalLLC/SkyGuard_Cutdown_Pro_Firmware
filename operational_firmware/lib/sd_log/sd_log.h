#pragma once
#include <Arduino.h>

void sdLogInit();
void sdLogUpdate1Hz(uint32_t now_ms);
bool sdLogIsReady();  // mounted + file known
