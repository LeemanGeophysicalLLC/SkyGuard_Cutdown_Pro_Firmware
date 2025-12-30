#pragma once
#include <Arduino.h>

void sdLogInit();
void sdLogUpdate1Hz(uint32_t now_ms);
bool sdLogIsReady();  // mounted + file known

// Flush any queued log lines to SD (safe to call when not in Iridium session).
void sdLogFlushQueued();

// For debugging/telemetry if you want it:
uint32_t sdLogQueuedCount();
uint32_t sdLogDroppedCount();

