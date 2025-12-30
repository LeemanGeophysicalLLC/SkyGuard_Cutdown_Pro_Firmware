#include <Arduino.h>
#include <esp_task_wdt.h> 

void startWatchdog(uint32_t timeoutSec) {
  // Initialize the task watchdog; panic=true means reboot on timeout
  esp_task_wdt_init(timeoutSec, true);
  // Add the current (main) task to the watchdog
  esp_task_wdt_add(nullptr);
}

void petWatchdog() {
  // Reset the task watchdog timer
  esp_task_wdt_reset();
}
