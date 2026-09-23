/**
 * vehicle_control.cpp - Vehicle control implementation
 *
 * Commands are resolved via ActiveProfileManager (built-in DBC or a
 * custom Learned/Manual profile) instead of hardcoded CAN IDs. See
 * CarTouch_SPEC.md section 7.1.
 *
 * Both the profile-driven path and the legacy path (setCustomCANIDs)
 * funnel through _sendResolvedMessage() / _sendLegacyCommand(), which
 * share the same rate limit and duty-cycle state.
 */

#include "vehicle_control.h"
#include "custom_vehicle.h"

// ============================================================================
// Constructor
// ============================================================================

VehicleControl::VehicleControl(CANManager& canManager, ActiveProfileManager& profileManager)
    : _can(canManager), _profileManager(profileManager) {
    _lastError          = 0;
    _lastErrorMessage   = "";
    _lastCommandTime     = 0;

    _legacyOverrideActive = false;
    _canIdDoorLock         = 0x1A0;
    _canIdWindow           = 0x1A1;
    _canIdSunroof          = 0x1A2;
    _canIdTrunk            = 0x1A3;
    _canIdMirror           = 0x1A4;
    _canIdAlarm            = 0x1A5;

    memset(&_windowDuty,  0, sizeof(_windowDuty));
    memset(&_sunroofDuty, 0, sizeof(_sunroofDuty));
    memset(&_mirrorDuty,  0, sizeof(_mirrorDuty));
}

void VehicleControl::begin() {
    Serial.println("[CTRL] Vehicle Control ready (profile-driven)");
}

// ============================================================================
// Message dispatch
// ============================================================================

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
        Serial.printf("[CTRL] Command sent: ID=0x%03X, data=", msg.id);
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

bool VehicleControl::_sendLegacyCommand(uint32_t canId, const uint8_t* data, uint8_t length) {
    CanMessage msg;
    msg.id          = canId;
    msg.isExtended  = false;
    msg.isRemote    = false;
    msg.length      = length;
    for (int i = 0; i < length && i < 8; i++) {
        msg.data[i] = data[i];
    }
    return _sendResolvedMessage(msg);
}

// ============================================================================
// Mechanical duty-cycle protection
// ============================================================================
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
    if (strncmp(label, "sunroof_", 8) == 0) return ACTUATOR_SUNROOF;
    if (strncmp(label, "mirror_",  7) == 0) return ACTUATOR_MIRROR;
    return ACTUATOR_NONE;
}

bool VehicleControl::_checkAndRecordDutyCycle(ActuatorClass cls, String& outErrorReason) {
    ActuatorDutyState* state = _dutyStateFor(cls);
    if (!state) return true;  // ACTUATOR_NONE: no limit applies

    uint32_t now = millis();

    // Reject outright while cooling down
    if (state->cooldownUntil != 0) {
        if (now < state->cooldownUntil) {
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
        state->cooldownUntil = now + ACTUATOR_DUTY_COOLDOWN_MS;
        outErrorReason = "Too many activations in a short period - please wait a few seconds";
        return false;
    }

    // Record this activation
    state->activationTimestamps[state->nextSlot] = now;
    state->nextSlot = (state->nextSlot + 1) % ACTUATOR_DUTY_MAX_ACTIVATIONS;
    if (state->count < ACTUATOR_DUTY_MAX_ACTIVATIONS) {
        state->count++;
    }

    return true;
}

// ============================================================================
// Label execution
// ============================================================================

bool VehicleControl::_execute(const char* label, String& outErrorReason) {
    // Runs before either dispatch path (legacy or ActiveProfileManager),
    // since the goal is protecting the physical motor regardless of
    // which software path resolved the command.
    ActuatorClass cls = _classifyLabel(label);
    if (cls != ACTUATOR_NONE) {
        if (!_checkAndRecordDutyCycle(cls, outErrorReason)) {
            _lastError = 4;
            _lastErrorMessage = outErrorReason;
            return false;
        }
    }

    if (_legacyOverrideActive) {
        // Legacy path: known labels map to fixed CAN IDs set via
        // setCustomCANIDs(). Does not go through the UNVERIFIED/VERIFIED
        // gate - prefer manual entry instead where possible.
        if (strcmp(label, CMD_LABEL_LOCK_ALL) == 0) {
            uint8_t d[] = {0x01};
            return _sendLegacyCommand(_canIdDoorLock, d, 1);
        } else if (strcmp(label, CMD_LABEL_UNLOCK_ALL) == 0) {
            uint8_t d[] = {0x02};
            return _sendLegacyCommand(_canIdDoorLock, d, 1);
        } else if (strcmp(label, CMD_LABEL_UNLOCK_DRIVER) == 0) {
            uint8_t d[] = {0x04};
            return _sendLegacyCommand(_canIdDoorLock, d, 1);
        } else if (strcmp(label, CMD_LABEL_ALL_WINDOWS_UP) == 0) {
            uint8_t d[] = {0xFF, 0x01};
            return _sendLegacyCommand(_canIdWindow, d, 2);
        } else if (strcmp(label, CMD_LABEL_ALL_WINDOWS_DOWN) == 0) {
            uint8_t d[] = {0xFF, 0x02};
            return _sendLegacyCommand(_canIdWindow, d, 2);
        } else if (strcmp(label, CMD_LABEL_SUNROOF_OPEN) == 0) {
            uint8_t d[] = {0x01};
            return _sendLegacyCommand(_canIdSunroof, d, 1);
        } else if (strcmp(label, CMD_LABEL_SUNROOF_CLOSE) == 0) {
            uint8_t d[] = {0x02};
            return _sendLegacyCommand(_canIdSunroof, d, 1);
        } else if (strcmp(label, CMD_LABEL_SUNROOF_TILT) == 0) {
            uint8_t d[] = {0x03};
            return _sendLegacyCommand(_canIdSunroof, d, 1);
        } else if (strcmp(label, CMD_LABEL_TRUNK_OPEN) == 0) {
            uint8_t d[] = {0x01};
            return _sendLegacyCommand(_canIdTrunk, d, 1);
        } else if (strcmp(label, CMD_LABEL_TRUNK_LOCK) == 0) {
            uint8_t d[] = {0x02};
            return _sendLegacyCommand(_canIdTrunk, d, 1);
        } else if (strcmp(label, CMD_LABEL_MIRROR_FOLD) == 0) {
            uint8_t d[] = {0x01};
            return _sendLegacyCommand(_canIdMirror, d, 1);
        } else if (strcmp(label, CMD_LABEL_MIRROR_UNFOLD) == 0) {
            uint8_t d[] = {0x02};
            return _sendLegacyCommand(_canIdMirror, d, 1);
        } else if (strcmp(label, CMD_LABEL_ALARM_ARM) == 0) {
            uint8_t d[] = {0x01};
            return _sendLegacyCommand(_canIdAlarm, d, 1);
        } else if (strcmp(label, CMD_LABEL_ALARM_DISARM) == 0) {
            uint8_t d[] = {0x02};
            return _sendLegacyCommand(_canIdAlarm, d, 1);
        } else if (strncmp(label, "window_", 7) == 0) {
            // window_fl_up, window_fr_down, ...
            uint8_t winIdx = 0xFF;
            bool up = strstr(label, "_up") != nullptr;
            if (strstr(label, "fl"))      winIdx = 0;
            else if (strstr(label, "fr")) winIdx = 1;
            else if (strstr(label, "rl")) winIdx = 2;
            else if (strstr(label, "rr")) winIdx = 3;
            if (winIdx == 0xFF) {
                outErrorReason = "Could not determine window index from legacy label";
                return false;
            }
            uint8_t d[] = {winIdx, (uint8_t)(up ? 0x01 : 0x02)};
            return _sendLegacyCommand(_canIdWindow, d, 2);
        }

        outErrorReason = "This label is not supported on the legacy compatibility path";
        return false;
    }

    // Primary path: resolve through ActiveProfileManager
    CanMessage msg;
    if (!_profileManager.resolveCommand(label, msg, outErrorReason)) {
        return false;
    }

    return _sendResolvedMessage(msg);
}

// ============================================================================
// Public execute API
// ============================================================================

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

// ============================================================================
// Convenience wrappers
// ============================================================================

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

// ============================================================================
// Stop all
// ============================================================================

bool VehicleControl::stopAll() {
    // Best-effort, legacy-only: without a learned "stop" command, this
    // returns false rather than guessing a CAN ID under a custom profile.
    if (!_legacyOverrideActive) {
        _lastErrorMessage = "Stop-all is only supported in legacy mode "
                             "or via specific learned commands";
        return false;
    }

    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    bool result = true;
    result &= _sendLegacyCommand(_canIdDoorLock, data, 8);
    result &= _sendLegacyCommand(_canIdWindow,   data, 8);
    result &= _sendLegacyCommand(_canIdSunroof,  data, 8);
    result &= _sendLegacyCommand(_canIdTrunk,    data, 8);
    return result;
}

// ============================================================================
// Legacy CAN ID override
// ============================================================================

void VehicleControl::setCustomCANIDs(uint32_t doorLock, uint32_t window,
                                      uint32_t sunroof, uint32_t trunk,
                                      uint32_t mirror, uint32_t alarm) {
    _canIdDoorLock = doorLock;
    _canIdWindow   = window;
    _canIdSunroof  = sunroof;
    _canIdTrunk    = trunk;
    _canIdMirror   = mirror;
    _canIdAlarm    = alarm;
    _legacyOverrideActive = true;

    Serial.println("[CTRL] Legacy manual override enabled - custom CAN IDs:");
    Serial.printf("  DoorLock=0x%03X, Window=0x%03X, Sunroof=0x%03X\n",
                  doorLock, window, sunroof);
    Serial.printf("  Trunk=0x%03X, Mirror=0x%03X, Alarm=0x%03X\n",
                  trunk, mirror, alarm);
    Serial.println("[CTRL] Note: the legacy path bypasses the UNVERIFIED/VERIFIED "
                    "safety gate. Prefer manual entry under 'My Commands' instead.");
}

// ============================================================================
// Error accessors
// ============================================================================

uint8_t VehicleControl::getLastError() {
    return _lastError;
}

String VehicleControl::getLastErrorMessage() {
    return _lastErrorMessage;
}
