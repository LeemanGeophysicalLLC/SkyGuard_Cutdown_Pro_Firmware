#pragma once

#include <Arduino.h>

// ---- Error codes -------------------------------------------------

enum ErrorCode {
    ERR_NONE = 0,

    ERR_ENV_SENSOR,
    ERR_SD_MISSING,
    ERR_SD_IO,
    ERR_GPS,
    ERR_IRIDIUM,
    ERR_UNSPECIFIED,

    ERR_COUNT
};

// ---- Severity ----------------------------------------------------

enum ErrorSeverity {
    ERROR_SEV_NONE = 0,
    ERROR_SEV_WARN,
    ERROR_SEV_CRIT
};

// ---- API ---------------------------------------------------------

void errorsInit();

// Latch / clear
void errorSet(ErrorCode code);
void errorClear(ErrorCode code);

// Queries
bool errorIsActive(ErrorCode code);
bool errorsAnyActive();
bool errorsAnyCriticalActive();

ErrorSeverity errorsGetOverallSeverity();

// Human-readable summary (static buffer, short)
const char* errorsGetSummaryString();
