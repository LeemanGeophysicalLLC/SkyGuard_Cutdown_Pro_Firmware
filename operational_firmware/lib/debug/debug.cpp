// debug.cpp
#include "debug.h"
#include <stdarg.h>

void debugInit() {
    // Always start Serial for deterministic boot behavior.
    Serial.begin(DEBUG_SERIAL_BAUD);
    // Small settle delay helps some USB-serial adapters; harmless if not present.
    delay(20);
}

void debugPrint(const char* msg) {
    if (!DEBUG_SERIAL) return;
    if (!msg) return;
    Serial.print(msg);
}

void debugPrintln(const char* msg) {
    if (!DEBUG_SERIAL) return;
    if (!msg) {
        Serial.println();
        return;
    }
    Serial.println(msg);
}
