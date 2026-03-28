// servo_release.h
#pragma once

/**
 * @file servo_release.h
 * @brief Servo-driven release mechanism control for SkyGuard Cutdown Pro.
 *
 * Design goals:
 *  - Deterministic, minimal, and easy to reason about.
 *  - Hard-coded servo positions (LOCK / RELEASE) for reliability.
 *  - Optional boot “wiggle” life-check (full stroke).
 *  - One-shot release latch: once released, we never re-arm until power cycle.
 *
 * Notes:
 *  - Web config mode may call LOCK / RELEASE for ground testing.
 *  - In flight, cut logic should only call servoReleaseRelease().
 */

#include <stdint.h>
#include <stdbool.h>

/**
 * @enum ServoReleaseState
 * @brief High-level release mechanism state.
 */
enum ServoReleaseState : uint8_t {
    SERVO_STATE_UNKNOWN = 0,   ///< Not initialized or not attached.
    SERVO_STATE_LOCKED,        ///< Servo is commanded to the lock position.
    SERVO_STATE_RELEASED       ///< Servo is commanded to release position (latched).
};

/**
 * @brief Initialize the servo release mechanism.
 *
 * Behavior:
 *  - Attaches the servo output.
 *  - Does NOT automatically wiggle (call servoReleaseWiggle() from main if desired).
 *  - Commands the servo to LOCK.
 *
 * Safety:
 *  - This MUST be called once at boot before any lock/release commands.
 */
void servoReleaseInit();

/**
 * @brief Perform a “wiggle” motion as a life check.
 *
 * New behavior (simple and obvious):
 *  - Command RELEASE, hold for ~2 seconds, then command LOCK.
 *
 * Notes:
 *  - This function is blocking and uses delays.
 *  - Call this only when it is safe to temporarily go to RELEASE (ground checks).
 */
void servoReleaseWiggle();

/**
 * @brief Command the mechanism to LOCK (ground test only).
 *
 * Important:
 *  - If the mechanism has already been released (latched), this does nothing and returns false.
 *
 * @return true if command accepted; false if ignored due to release latch or not initialized.
 */
bool servoReleaseLock();

/**
 * @brief Command the mechanism to RELEASE (one-shot latch).
 *
 * Important:
 *  - This function latches "released" until power cycle.
 *  - Calling it multiple times is safe; subsequent calls are ignored but return true.
 *
 * @return true if mechanism is (or is now) released; false if not initialized.
 */
bool servoReleaseRelease();

/**
 * @brief Command LOCK for bench testing without changing the flight latch.
 *
 * Intended only for config-mode ground testing. Unlike servoReleaseLock(), this
 * ignores the one-shot release latch and simply commands the mechanism.
 *
 * @return true if the servo command was issued; false if not initialized.
 */
bool servoReleaseLockForTest();

/**
 * @brief Command RELEASE for bench testing without changing the flight latch.
 *
 * Intended only for config-mode ground testing. Unlike servoReleaseRelease(),
 * this does not permanently latch the release state.
 *
 * @return true if the servo command was issued; false if not initialized.
 */
bool servoReleaseReleaseForTest();

/**
 * @brief Get current mechanism state (best-effort).
 *
 * @return Current ServoReleaseState.
 */
ServoReleaseState servoReleaseGetState();

/**
 * @brief Return true if release has been latched.
 *
 * @return true if released; false otherwise.
 */
bool servoReleaseIsReleased();
