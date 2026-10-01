/**
 * vehicle_control.h - Vehicle component control over CAN Bus
 *
 * Commands are resolved by label (e.g. "lock_all") through
 * ActiveProfileManager, which maps the label to a real CanMessage based
 * on the active vehicle profile (built-in DBC, or a Learned/Manual
 * custom profile). No CAN IDs are hardcoded here.
 *
 * If the active profile is a custom one and the requested command is
 * still UNVERIFIED, execution fails (returns false) by design - the
 * device never sends guessed or zero-filled frames.
 */

#ifndef VEHICLE_CONTROL_H
#define VEHICLE_CONTROL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"
#include "can_manager.h"
#include "active_profile_manager.h"

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Control interface
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

class VehicleControl {

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Public API
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

public:
    VehicleControl(CanInterface& canInterface, ActiveProfileManager& profileManager);

    void begin();

    // -- Label-driven execution (primary API) -----------------------------------
    bool executeCommand(const char* commandLabel);
    bool executeCommand(const char* commandLabel, String& outErrorReason);

    /**
     * One-shot Learn Mode verification send: the same safety checks as
     * executeCommand() (Listen-Only, rate limit, duty-cycle), but
     * resolves the command via
     * ActiveProfileManager::resolveCommandForVerification() so an
     * UNVERIFIED custom command is allowed through exactly once. Never
     * changes the command's VERIFIED/UNVERIFIED status itself - that is
     * decided only by the caller's explicit user confirmation
     * afterward. Must be called ONLY from the dedicated verification UI
     * flow (TFT verify screen / webserver "verify_command" handler).
     */
    bool executeCommandForVerification(const char* commandLabel, String& outErrorReason);

    // -- Doors --------------------------------------------------------------------
    bool lockAllDoors();
    bool unlockAllDoors();
    bool unlockDriverDoor();

    // -- Windows (0=FL, 1=FR, 2=RL, 3=RR) -----------------------------------------
    bool windowUp(uint8_t window);
    bool windowDown(uint8_t window);
    bool allWindowsUp();
    bool allWindowsDown();

    // -- Sunroof --------------------------------------------------------------------
    bool sunroofOpen();
    bool sunroofClose();
    bool sunroofTilt();

    // -- Trunk -----------------------------------------------------------------------
    bool trunkOpen();
    bool trunkLock();

    // -- Mirrors ---------------------------------------------------------------------
    bool foldMirrors();
    bool unfoldMirrors();

    // -- Alarm -----------------------------------------------------------------------
    bool alarmArm();
    bool alarmDisarm();

    /**
     * Request that any in-progress actuator movement stop. This is a
     * best-effort, profile-driven request ONLY: it resolves the
     * "stop_all" label through ActiveProfileManager::resolveCommand(),
     * so it is subject to exactly the same CMD_VERIFIED gate, Listen-Only
     * guard, rate limit and duty-cycle rules as every other command. If
     * the active profile does not define a "stop_all" command, this
     * returns false and nothing is sent - it never guesses a CAN ID or
     * emits an all-zero frame on an arbitrary ID.
     */
    bool stopAll();

    uint8_t getLastError();
    String  getLastErrorMessage();

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Safety and actuator state
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

private:
    SemaphoreHandle_t       _mutex;    // serializes _execute() across tasks
    CanInterface&           _can;
    ActiveProfileManager&   _profileManager;
    uint8_t                 _lastError;
    String                  _lastErrorMessage;

    // -- Base rate limit (applies to every command, any actuator) -----------------
    uint32_t _lastCommandTime;
    static const uint32_t MIN_COMMAND_INTERVAL_MS = 150;

    // -- Duty-cycle protection (motorized actuators only) --------------------------
    //
    // MIN_COMMAND_INTERVAL_MS only spaces out consecutive commands; it does
    // not stop sustained/repeated activation of a single motor (window,
    // sunroof, mirror) over a longer window, which is what actually causes
    // wear or overheating. This adds a cumulative, per-actuator-class
    // limit: once ACTUATOR_DUTY_MAX_ACTIVATIONS commands land within
    // ACTUATOR_DUTY_WINDOW_MS, further commands for that class are
    // rejected until a cooldown period elapses. Relay-based actuators
    // (locks, alarm) are not motors and are exempt - they only go through
    // the base rate limit above.
    enum ActuatorClass : uint8_t {
        ACTUATOR_NONE = 0,    // No duty-cycle limit (locks, alarm, trunk)
        ACTUATOR_WINDOW,      // All 4 windows share one counter
        ACTUATOR_SUNROOF,
        ACTUATOR_MIRROR
    };
    static const uint32_t ACTUATOR_DUTY_WINDOW_MS       = 10000;    // Rolling window
    static const uint8_t  ACTUATOR_DUTY_MAX_ACTIVATIONS = 6;        // Max activations per window
    static const uint32_t ACTUATOR_DUTY_COOLDOWN_MS     = 5000;     // Forced rest once the cap is hit

    struct ActuatorDutyState {
        uint32_t activationTimestamps[ACTUATOR_DUTY_MAX_ACTIVATIONS];    // Circular buffer
        uint8_t  count;                                                  // Entries recorded so far
        uint8_t  nextSlot;                                               // Next write index
        uint32_t cooldownUntil;                                          // 0 = no active cooldown
    };
    ActuatorDutyState _windowDuty;
    ActuatorDutyState _sunroofDuty;
    ActuatorDutyState _mirrorDuty;

    bool _checkDutyCycle(ActuatorClass cls, String& outErrorReason);
    void _recordDutyCycle(ActuatorClass cls);
    ActuatorDutyState* _dutyStateFor(ActuatorClass cls);

    // Classifies a command label by string prefix (window_/sunroof_/mirror_).
    // NOTE: a free-text custom label from Learn Mode that doesn't use these
    // prefixes will not be classified, and will only get the base rate
    // limit above - not the duty-cycle limit.
    static ActuatorClass _classifyLabel(const char* label);

    bool _sendResolvedMessage(const CanMessage& msg);
    bool _execute(const char* label, String& outErrorReason, bool forVerification = false);
};

#endif    // VEHICLE_CONTROL_H
