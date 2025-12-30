#include <Arduino.h>
/**
 * @brief Initialize and start the ESP32 task watchdog.
 *
 * @param timeoutSec  How many seconds until the watchdog fires.
 *                    Defaults to CONFIG_WATCHDOG_TIMEOUT_SEC.
 */
void startWatchdog(uint32_t timeoutSec = 10);

/**
 * @brief “Pet” or reset the watchdog timer. Call this periodically
 *        (e.g. once per loop iteration) to prevent a system reset.
 */
void petWatchdog();
