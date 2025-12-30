#include "errors.h"

// ---- Internal structures ----------------------------------------

struct ErrorEntry {
    bool active;
    uint32_t first_seen_s;
};

static ErrorEntry g_errors[ERR_COUNT];

// ---- Severity map (compile-time) --------------------------------

static ErrorSeverity errorSeverityMap[ERR_COUNT] = {
    ERROR_SEV_NONE,  // ERR_NONE

    ERROR_SEV_CRIT,  // ERR_ENV_SENSOR
    ERROR_SEV_WARN,  // ERR_SD_MISSING
    ERROR_SEV_CRIT,  // ERR_SD_IO
    ERROR_SEV_CRIT,  // ERR_GPS
    ERROR_SEV_CRIT,  // ERR_IRIDIUM
    ERROR_SEV_CRIT   // ERR_UNSPECIFIED
};

// ---- Names for summary output -----------------------------------

static const char* errorNameMap[ERR_COUNT] = {
    "None",
    "Env sensor",
    "SD missing",
    "SD I/O",
    "GPS",
    "Iridium",
    "Unspecified"
};

// ---- Public API --------------------------------------------------

void errorsInit() {
    for (int i = 0; i < ERR_COUNT; i++) {
        g_errors[i].active = false;
        g_errors[i].first_seen_s = 0;
    }
}

void errorSet(ErrorCode code) {
    if (code <= ERR_NONE || code >= ERR_COUNT) {
        return;
    }

    if (!g_errors[code].active) {
        g_errors[code].active = true;
        g_errors[code].first_seen_s = millis() / 1000;
    }
}

void errorClear(ErrorCode code) {
    if (code <= ERR_NONE || code >= ERR_COUNT) {
        return;
    }

    g_errors[code].active = false;
    g_errors[code].first_seen_s = 0;
}

bool errorIsActive(ErrorCode code) {
    if (code <= ERR_NONE || code >= ERR_COUNT) {
        return false;
    }
    return g_errors[code].active;
}

bool errorsAnyActive() {
    for (int i = 1; i < ERR_COUNT; i++) {
        if (g_errors[i].active) {
            return true;
        }
    }
    return false;
}

bool errorsAnyCriticalActive() {
    for (int i = 1; i < ERR_COUNT; i++) {
        if (g_errors[i].active &&
            errorSeverityMap[i] == ERROR_SEV_CRIT) {
            return true;
        }
    }
    return false;
}

ErrorSeverity errorsGetOverallSeverity() {
    bool has_warn = false;

    for (int i = 1; i < ERR_COUNT; i++) {
        if (!g_errors[i].active) {
            continue;
        }

        if (errorSeverityMap[i] == ERROR_SEV_CRIT) {
            return ERROR_SEV_CRIT;
        }

        if (errorSeverityMap[i] == ERROR_SEV_WARN) {
            has_warn = true;
        }
    }

    if (has_warn) {
        return ERROR_SEV_WARN;
    }

    return ERROR_SEV_NONE;
}

const char* errorsGetSummaryString() {
    static char summary[128];
    summary[0] = '\0';

    bool first = true;

    for (int i = 1; i < ERR_COUNT; i++) {
        if (!g_errors[i].active) {
            continue;
        }

        if (!first) {
            strlcat(summary, ", ", sizeof(summary));
        }

        strlcat(summary, errorNameMap[i], sizeof(summary));
        first = false;
    }

    if (summary[0] == '\0') {
        return "OK";
    }

    return summary;
}
