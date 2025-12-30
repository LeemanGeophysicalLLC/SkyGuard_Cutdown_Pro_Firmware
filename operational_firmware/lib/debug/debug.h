// debug.h
#pragma once

/**
 * @file debug.h
 * @brief Simple debug print helpers with a compile-time enable flag.
 *
 * Notes:
 *  - Serial is always initialized to keep boot deterministic.
 *  - When DEBUG_LOGS_ENABLED is false, debugPrint* calls do nothing.
 */

#include <Arduino.h>
#include <stdint.h>
#include "project_config.h"

/**
 * @brief Always initialize Serial for deterministic behavior.
 *
 * Call once in setup().
 */
void debugInit();

/**
 * @brief Print a message (no newline) if debug logs enabled.
 */
void debugPrint(const char* msg);

/**
 * @brief Print a message with newline if debug logs enabled.
 */
void debugPrintln(const char* msg);
