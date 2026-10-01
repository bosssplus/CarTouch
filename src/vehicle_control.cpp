/**
 * vehicle_control.cpp - Vehicle control implementation
 *
 * Commands are resolved via ActiveProfileManager (built-in DBC or a
 * custom Learned/Manual profile) instead of hardcoded CAN IDs. See
 * CarTouch_SPEC.md.
 *
 * There is exactly ONE dispatch path: every command, including the
 * dedicated one-shot verification send, is resolved through
 * ActiveProfileManager and then funneled through
 * _sendResolvedMessage(), which applies the Listen-Only guard, the base
 * rate limit and (via _execute) the duty-cycle limit. There is no
 * hardcoded-CAN-ID / legacy override path that could bypass the
 * UNVERIFIED/VERIFIED safety gate.
 */

#include "vehicle_control.h"
#include "custom_vehicle.h"

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Constructor
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

VehicleControl::VehicleControl(CanInterface& canInterface, ActiveProfileManager& profileManager)
    : _can(canInterface), _profileManager(profileManager) {
    _mutex              = xSemaphoreCreateRecursiveMutex();
    _lastError          = 0;
    _lastErrorMessage   = "";
    _lastCommandTime     = 0;

    memset(&_windowDuty,  0, sizeof(_windowDuty));
    memset(&_sunroofDuty, 0, sizeof(_sunroofDuty));
    memset(&_mirrorDuty,  0, sizeof(_mirrorDuty));
}

void VehicleControl::begin() {
    Serial.println("[CTRL] Vehicle Control ready (profile-driven)");
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Message dispatch
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool VehicleControl::_sendResolvedMessage(const CanMessage& msg) {
    if (getConfig()->listenOnlyMode) {
        Serial.println("[CTRL] Listen-Only mode active - command not sent");
        _lastError = 1;
        _lastErrorMessage = "Listen-Only mode is active";
        return false;
    }

    uint32_t now = millis();
    if (now - _lastCommandTime < MIN_COMMAND_INTERVAL_MS) {
        Serial.println("[CTRL] Command rejected - rate limit");
        _lastError = 3;
        _lastErrorMessage = "Commands sent too rapidly (rate limit)";
        return false;
    }
    _lastCommandTime = now;

    if (_can.sendMessage(msg)) {
        Serial.printf("[CTRL] Command sent: ID=0x%03lX, data=", (unsigned long)msg.id);
        for (int i = 0; i < msg.length; i++) {
            Serial.printf("%02X ", msg.data[i]);
        }
        Serial.println();
        _lastError = 0;
        _lastErrorMessage = "";
        return true;
    }

    _lastError = 2;
    _lastErrorMessage = "CAN Bus send failed";
    return false;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Mechanical duty-cycle protection
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

//
// Prevents sustained or rapid-fire activation of a single motorized
// actuator (window/sunroof/mirror), which MIN_COMMAND_INTERVAL_MS alone
// does not cover - it only spaces out consecutive commands regardless of
// target actuator.
//
// A circular buffer of activation timestamps is kept per actuator class.
// Once ACTUATOR_DUTY_MAX_ACTIVATIONS occur within ACTUATOR_DUTY_WINDOW_MS,
// a cooldown starts during which no further command for that class is
// accepted, regardless of direction (up/down, open/close).
//
// NOTE: this is a software-level approximation, not a real current/
// temperature measurement. The defaults (6 activations / 10s, 5s cooldown)
// are conservative starting points and may need tuning per vehicle.

VehicleControl::ActuatorDutyState* VehicleControl::_dutyStateFor(ActuatorClass cls) {
    switch (cls) {
        case ACTUATOR_WINDOW:  return &_windowDuty;
        case ACTUATOR_SUNROOF: return &_sunroofDuty;
        case ACTUATOR_MIRROR:  return &_mirrorDuty;
        default:                return nullptr;
    }
}

VehicleControl::ActuatorClass VehicleControl::_classifyLabel(const char* label) {
    if (!label) return ACTUATOR_NONE;
    // Prefixes match the CMD_LABEL_* constants in custom_vehicle.h.
    // A free-text custom label that doesn't use these prefixes will not
    // be classified here, and will only get the base rate limit above.
    if (strncmp(label, "window_",  7) == 0) return ACTUATOR_WINDOW;
    if (strncmp(label, "all_windows_", 12) == 0) return ACTUATOR_WINDOW;
    if (strncmp(label, "sunroof_", 8) == 0) return ACTUATOR_SUNROOF;
    if (strncmp(label, "mirror_",  7) == 0) return ACTUATOR_MIRROR;
    return ACTUATOR_NONE;
}

bool VehicleControl::_checkDutyCycle(ActuatorClass cls, String& outErrorReason) {
    ActuatorDutyState* state = _dutyStateFor(cls);
    if (!state) return true;                          // ACTUATOR_NONE: no limit applies

    uint32_t now = millis();

    // Reject outright while cooling down
    if (state->cooldownUntil != 0) {
        // Wrap-safe: the cooldown itself is only a few seconds, so the
        // signed difference remains correct across a millis() rollover
        // (a plain "now < cooldownUntil" test would not).
        if ((int32_t)(now - state->cooldownUntil) < 0) {
            outErrorReason = "This component needs a brief rest after recent repeated use";
            return false;
        }
        // Cooldown elapsed - reset for a fresh window
        state->cooldownUntil = 0;
        state->count    = 0;
        state->nextSlot = 0;
    }

    // Count activations still inside the rolling window (no array
    // compaction needed - just a conditional count)
    uint8_t recentCount = 0;
    for (uint8_t i = 0; i < state->count; i++) {
        if (now - state->activationTimestamps[i] <= ACTUATOR_DUTY_WINDOW_MS) {
            recentCount++;
        }
    }

    if (recentCount >= ACTUATOR_DUTY_MAX_ACTIVATIONS) {
        uint32_t until = now + ACTUATOR_DUTY_COOLDOWN_MS;
        // 0 is the "no cooldown" sentinel - avoid colliding with it.
        state->cooldownUntil = (until == 0) ? 1 : until;
        outErrorReason = "Too many activations in a short period - please wait a few seconds";
        return false;
    }

    return true;
}

void VehicleControl::_recordDutyCycle(ActuatorClass cls) {
    ActuatorDutyState* state = _dutyStateFor(cls);
    if (!state) return;

    uint32_t now = millis();
    state->activationTimestamps[state->nextSlot] = now;
    state->nextSlot = (state->nextSlot + 1) % ACTUATOR_DUTY_MAX_ACTIVATIONS;
    if (state->count < ACTUATOR_DUTY_MAX_ACTIVATIONS) {
        state->count++;
    }
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Label execution
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

bool VehicleControl::_execute(const char* label, String& outErrorReason, bool forVerification) {
    struct Lock {
        SemaphoreHandle_t m;
        explicit Lock(SemaphoreHandle_t h) : m(h) { if (m) xSemaphoreTakeRecursive(m, portMAX_DELAY); }
        ~Lock() { if (m) xSemaphoreGiveRecursive(m); }
    } lock(_mutex);
    // Runs before ActiveProfileManager resolution, since the goal is
    // protecting the physical motor regardless of which command is
    // requested.
    ActuatorClass cls = _classifyLabel(label);
    if (cls != ACTUATOR_NONE) {
        if (!_checkDutyCycle(cls, outErrorReason)) {
            _lastError = 4;
            _lastErrorMessage = outErrorReason;
            return false;
        }
    }

    // Primary path: resolve through ActiveProfileManager. Normal
    // dispatch always keeps the CMD_VERIFIED gate (resolveCommand());
    // only the dedicated one-shot verification flow bypasses it
    // (resolveCommandForVerification()), while every check below
    // (Listen-Only, rate limit, duty-cycle) still applies either way.
    CanMessage msg;
    bool resolved = forVerification
        ? _profileManager.resolveCommandForVerification(label, msg, outErrorReason)
        : _profileManager.resolveCommand(label, msg, outErrorReason);
    if (!resolved) {
        return false;
    }

    bool sent = _sendResolvedMessage(msg);
    if (sent && cls != ACTUATOR_NONE) _recordDutyCycle(cls);
    return sent;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Public execute API
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool VehicleControl::executeCommand(const char* commandLabel) {
    String reason;
    return executeCommand(commandLabel, reason);
}

bool VehicleControl::executeCommand(const char* commandLabel, String& outErrorReason) {
    bool result = _execute(commandLabel, outErrorReason);
    if (!result && outErrorReason.length() > 0) {
        _lastErrorMessage = outErrorReason;
        Serial.printf("[CTRL] Command '%s' failed: %s\n",
                      commandLabel, outErrorReason.c_str());
    }
    return result;
}

bool VehicleControl::executeCommandForVerification(const char* commandLabel, String& outErrorReason) {
    bool result = _execute(commandLabel, outErrorReason, /*forVerification=*/true);
    if (!result && outErrorReason.length() > 0) {
        _lastErrorMessage = outErrorReason;
        Serial.printf("[CTRL] Verification send '%s' failed: %s\n",
                      commandLabel, outErrorReason.c_str());
    }
    return result;
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Convenience wrappers
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

bool VehicleControl::lockAllDoors() {
    return executeCommand(CMD_LABEL_LOCK_ALL);
}

bool VehicleControl::unlockAllDoors() {
    return executeCommand(CMD_LABEL_UNLOCK_ALL);
}

bool VehicleControl::unlockDriverDoor() {
    return executeCommand(CMD_LABEL_UNLOCK_DRIVER);
}

bool VehicleControl::windowUp(uint8_t window) {
    if (window > 3) return false;
    static const char* labels[4] = {
        CMD_LABEL_WINDOW_FL_UP, CMD_LABEL_WINDOW_FR_UP,
        CMD_LABEL_WINDOW_RL_UP, CMD_LABEL_WINDOW_RR_UP
    };
    return executeCommand(labels[window]);
}

bool VehicleControl::windowDown(uint8_t window) {
    if (window > 3) return false;
    static const char* labels[4] = {
        CMD_LABEL_WINDOW_FL_DOWN, CMD_LABEL_WINDOW_FR_DOWN,
        CMD_LABEL_WINDOW_RL_DOWN, CMD_LABEL_WINDOW_RR_DOWN
    };
    return executeCommand(labels[window]);
}

bool VehicleControl::allWindowsUp() {
    return executeCommand(CMD_LABEL_ALL_WINDOWS_UP);
}

bool VehicleControl::allWindowsDown() {
    return executeCommand(CMD_LABEL_ALL_WINDOWS_DOWN);
}

bool VehicleControl::sunroofOpen() {
    return executeCommand(CMD_LABEL_SUNROOF_OPEN);
}

bool VehicleControl::sunroofClose() {
    return executeCommand(CMD_LABEL_SUNROOF_CLOSE);
}

bool VehicleControl::sunroofTilt() {
    return executeCommand(CMD_LABEL_SUNROOF_TILT);
}

bool VehicleControl::trunkOpen() {
    return executeCommand(CMD_LABEL_TRUNK_OPEN);
}

bool VehicleControl::trunkLock() {
    return executeCommand(CMD_LABEL_TRUNK_LOCK);
}

bool VehicleControl::foldMirrors() {
    return executeCommand(CMD_LABEL_MIRROR_FOLD);
}

bool VehicleControl::unfoldMirrors() {
    return executeCommand(CMD_LABEL_MIRROR_UNFOLD);
}

bool VehicleControl::alarmArm() {
    return executeCommand(CMD_LABEL_ALARM_ARM);
}

bool VehicleControl::alarmDisarm() {
    return executeCommand(CMD_LABEL_ALARM_DISARM);
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Stop all
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool VehicleControl::stopAll() {
    // Profile-driven only. "stop_all" must be defined by the active
    // profile (built-in DBC or a verified custom command) exactly like
    // any other label - so this goes through resolveCommand()'s
    // CMD_VERIFIED gate, the Listen-Only guard and the base rate limit.
    // If no such command exists, nothing is sent; we never emit an
    // all-zero frame on a guessed CAN ID.
    String reason;
    bool ok = executeCommand("stop_all", reason);
    if (!ok) {
        _lastErrorMessage = reason.length() > 0
            ? reason
            : String("No 'stop_all' command is defined by the active profile");
    }
    return ok;
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Error accessors
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

uint8_t VehicleControl::getLastError() {
    return _lastError;
}

String VehicleControl::getLastErrorMessage() {
    return _lastErrorMessage;
}
