// servo_release.cpp
/**
 * @file servo_release.cpp
 * @brief Implementation of servo-driven release mechanism control.
 */

#include "servo_release.h"

#include "pins.h"
#include "project_config.h"

#include <Arduino.h>
#include <ESP32Servo.h>

// -------------------------
// Hard-coded servo tuning
// -------------------------

/**
 * @brief Servo pulse width limits (microseconds).
 *
 * Many hobby servos work in ~500..2500 us. These limits help ESP32Servo map degrees to pulses.
 * If your servo is picky, tune these values.
 */
static constexpr int SERVO_MIN_US = 500;
static constexpr int SERVO_MAX_US = 2500;

/**
 * @brief Servo positions in degrees.
 *
 * Hard-coded per project spec. Tune on the bench and keep stable.
 */
static constexpr int SERVO_POS_LOCK_DEG    = 15;
static constexpr int SERVO_POS_RELEASE_DEG = 120;

/**
 * @brief Timing for the life-check wiggle.
 */
static constexpr uint32_t WIGGLE_HOLD_MS = 2000;

// -------------------------
// Internal module state
// -------------------------

static Servo g_servo;
static bool g_attached = false;
static bool g_released_latched = false;
static ServoReleaseState g_state = SERVO_STATE_UNKNOWN;

/**
 * @brief Attach servo if not already attached.
 *
 * @return true if attached (or already attached); false on failure.
 */
static bool ensureAttached() {
    if (g_attached) return true;

    g_servo.setPeriodHertz(50); // standard servo frequency
    g_servo.attach(PIN_SERVO, SERVO_MIN_US, SERVO_MAX_US);

    g_attached = g_servo.attached();
    if (!g_attached) {
        g_state = SERVO_STATE_UNKNOWN;
        return false;
    }

    // Small settle delay after attach helps some servos behave consistently.
    delay(100);
    return true;
}

/**
 * @brief Command servo to an angle, clamped to [0, 180].
 *
 * @param deg Angle in degrees.
 */
static void writeAngleClamped(int deg) {
    if (deg < 0) deg = 0;
    if (deg > 180) deg = 180;
    g_servo.write(deg);
}

static void detachServo() {
    if (!g_attached) return;
    g_servo.detach();
    g_attached = false;
}

/**
 * @brief Set internal state after commanding motion.
 */
static void setState(ServoReleaseState st) {
    g_state = st;
}

/**
 * @brief Attach, move, hold briefly, then detach to avoid continuous servo power.
 */
static bool moveServoAndDetach(int deg, uint32_t hold_ms, ServoReleaseState st) {
    if (!ensureAttached()) return false;

    writeAngleClamped(deg);
    setState(st);
    delay(hold_ms);
    detachServo();
    return true;
}

// -------------------------
// Public API
// -------------------------

void servoReleaseInit() {
    /**
     * Initialize servo output and command LOCK.
     *
     * Wiggle is NOT automatic now — call servoReleaseWiggle() from main when appropriate.
     */
    g_released_latched = false;
    g_state = SERVO_STATE_UNKNOWN;
    (void)servoReleaseLock();
}

void servoReleaseWiggle() {
    /**
     * Simple life check:
     *  - go to RELEASE, hold, then return to LOCK.
     *
     * Call only when it is safe to temporarily enter RELEASE.
     */
    // IMPORTANT: Wiggle is a diagnostic action. It must NOT latch "released".
    // We intentionally command release position without setting g_released_latched.
    if (!moveServoAndDetach(SERVO_POS_RELEASE_DEG, WIGGLE_HOLD_MS, SERVO_STATE_RELEASED)) {
        return;
    }

    (void)moveServoAndDetach(SERVO_POS_LOCK_DEG, SERVO_MOVE_HOLD_MS, SERVO_STATE_LOCKED);
}

bool servoReleaseLock() {
    /**
     * Lock is allowed ONLY if we have not latched a release.
     */
    if (g_released_latched) {
        setState(SERVO_STATE_RELEASED);
        return false;
    }

    return moveServoAndDetach(SERVO_POS_LOCK_DEG, SERVO_MOVE_HOLD_MS, SERVO_STATE_LOCKED);
}

bool servoReleaseRelease() {
    /**
     * Release is a one-shot latch.
     */
    if (!g_released_latched) {
        g_released_latched = true;
        return moveServoAndDetach(SERVO_POS_RELEASE_DEG, SERVO_MOVE_HOLD_MS, SERVO_STATE_RELEASED);
    }

    return true;
}

bool servoReleaseLockForTest() {
    return moveServoAndDetach(SERVO_POS_LOCK_DEG, SERVO_MOVE_HOLD_MS, SERVO_STATE_LOCKED);
}

bool servoReleaseReleaseForTest() {
    return moveServoAndDetach(SERVO_POS_RELEASE_DEG, SERVO_MOVE_HOLD_MS, SERVO_STATE_RELEASED);
}

ServoReleaseState servoReleaseGetState() {
    /**
     * Best-effort state: reflects last commanded state, not actual mechanics.
     */
    return g_state;
}

bool servoReleaseIsReleased() {
    return g_released_latched;
}
