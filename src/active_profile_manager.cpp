/**
 * active_profile_manager.cpp - Command-source unification layer implementation
 */

#include "active_profile_manager.h"
#include "custom_vehicle.h"
#include "ct_tx_guard.h"

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Constructor
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

ActiveProfileManager::ActiveProfileManager(VehicleDB& vehicleDB, CustomVehicleStore& customStore)
    : _vehicleDB(vehicleDB), _customStore(customStore) {
    _activeKind           = ACTIVE_KIND_NONE;
    _activeCustomIndex       = 0;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Select a DBC vehicle
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool ActiveProfileManager::selectDBCVehicle(const char* brand, const char* model) {
    // Only switch the active kind when the vehicle really exists and its DBC
    // loaded; otherwise keep the previous (known-good) selection.
    if (!_vehicleDB.setActiveVehicle(brand, model)) {
        Serial.printf("[APM] DBC vehicle not selected: %s %s\n", brand, model);
        return false;
    }
    _activeKind = ACTIVE_KIND_DBC;
    Serial.printf("[APM] Active vehicle (DBC): %s %s\n", brand, model);
    return true;
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Select a custom vehicle
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

bool ActiveProfileManager::selectCustomVehicle(uint8_t profileIndex) {
    CustomVehicleProfile summary;
    if (!_customStore.getProfileSummary(profileIndex, summary)) {
        Serial.printf("[APM] Custom profile %d not found\n", profileIndex);
        return false;
    }

    _activeCustomIndex = profileIndex;
    _activeKind            = ACTIVE_KIND_CUSTOM;
    Serial.printf("[APM] Active vehicle (custom): %s\n", summary.name);
    return true;
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Accessors
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

ActiveVehicleKind ActiveProfileManager::getActiveKind() {
    return _activeKind;
}

uint8_t ActiveProfileManager::getActiveCustomIndex() {
    return _activeCustomIndex;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Clear active custom selection
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void ActiveProfileManager::clearActiveIfCustom(uint8_t profileIndex) {
    if (_activeKind == ACTIVE_KIND_CUSTOM && _activeCustomIndex == profileIndex) {
        _activeKind = ACTIVE_KIND_NONE;
        _activeCustomIndex = 0;
        Serial.printf("[APM] Active custom profile %d deleted - active selection cleared\n", profileIndex);
    }
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Command resolution
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool ActiveProfileManager::resolveCommand(const char* label, CanMessage& outMsg, String& outErrorReason) {
    if (_activeKind == ACTIVE_KIND_NONE) {
        outErrorReason = "No vehicle selected";
        return false;
    }

    if (_activeKind == ACTIVE_KIND_DBC) {
        // -- DBC path ---------------------------------------------------------
        // Important note (see CarTouch_SPEC.md): most DBC files bundled
        // with this project (from OpenDBC) mainly contain read signals,
        // not write commands for locks/windows. The current
        // implementation doesn't rely on any specific naming convention
        // for finding write commands in a DBC (since such data is rarely
        // actually present in the bundled files in practice). So the DBC
        // path currently returns an honest error message, so the user
        // knows exactly why instead of a silent failure, and is guided
        // toward Learn Mode / manual entry.
        //
        // If a signal-naming convention (e.g. a signal named exactly
        // like the label, such as "lock_all") is ever identified and
        // confirmed in a specific DBC, this is where
        // vehicleDB.findMessageByID / findSignal for that signal and
        // encodeSignalValue should be used. This is deliberately not
        // implemented - guessing at such a convention
        // without real data would be riskier than being honest about
        // its absence.
        outErrorReason = "This DBC file has no write command for this function - "
                         "use Learn Mode or Manual Entry";
        return false;
    }

    // -- Custom path (Learned / Manual) ----------------------------------------
    LearnedCommand cmd;
    if (!_customStore.findCommand(_activeCustomIndex, label, cmd)) {
        outErrorReason = "No command with this label is registered for this vehicle";
        return false;
    }

    // Critical safety check - defense in depth. vehicle_control.cpp also
    // has this check, but it's repeated here too, so this class alone
    // (e.g. if called from somewhere else in the future) never returns
    // an unverified command as sendable.
    if (cmd.status != CMD_VERIFIED) {
        outErrorReason = "This command is still UNVERIFIED - "
                         "verify it from My Commands";
        return false;
    }

    if (cmd.length > 8 || !ctTxIdValid(cmd.canId, cmd.isExtended)) {
        outErrorReason = "Stored command has an invalid CAN ID or length - re-create it";
        return false;
    }

    outMsg.id          = cmd.canId;
    outMsg.isExtended    = cmd.isExtended;
    outMsg.isRemote        = false;
    outMsg.length             = cmd.length;
    memcpy(outMsg.data, cmd.data, cmd.length);

    return true;
}

bool ActiveProfileManager::resolveCommandForVerification(const char* label, CanMessage& outMsg, String& outErrorReason) {
    if (_activeKind == ACTIVE_KIND_NONE) {
        outErrorReason = "No vehicle selected";
        return false;
    }

    if (_activeKind == ACTIVE_KIND_DBC) {
        outErrorReason = "This DBC file has no write command for this function - "
                         "use Learn Mode or Manual Entry";
        return false;
    }

    LearnedCommand cmd;
    if (!_customStore.findCommand(_activeCustomIndex, label, cmd)) {
        outErrorReason = "No command with this label is registered for this vehicle";
        return false;
    }

    // Verification is only for commands that are still unverified.
    if (cmd.status != CMD_UNVERIFIED) {
        outErrorReason = "This command is already verified";
        return false;
    }

    // Every other safety mechanism (Listen-Only, rate limiting,
    // duty-cycle) still applies in VehicleControl.
    if (cmd.length > 8 || !ctTxIdValid(cmd.canId, cmd.isExtended)) {
        outErrorReason = "Stored command has an invalid CAN ID or length - re-create it";
        return false;
    }

    outMsg.id          = cmd.canId;
    outMsg.isExtended    = cmd.isExtended;
    outMsg.isRemote        = false;
    outMsg.length             = cmd.length;
    memcpy(outMsg.data, cmd.data, cmd.length);

    return true;
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Active vehicle name
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

void ActiveProfileManager::getActiveVehicleName(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;

    if (_activeKind == ACTIVE_KIND_DBC) {
        char brand[24], model[24];
        _vehicleDB.getActiveVehicle(brand, model, sizeof(brand));
        snprintf(outBuf, maxLen, "%s %s", brand, model);
    } else if (_activeKind == ACTIVE_KIND_CUSTOM) {
        CustomVehicleProfile summary;
        if (_customStore.getProfileSummary(_activeCustomIndex, summary)) {
            strncpy(outBuf, summary.name, maxLen - 1);
            outBuf[maxLen - 1] = '\0';
        } else {
            strncpy(outBuf, "?", maxLen - 1);
            outBuf[maxLen - 1] = '\0';
        }
    } else {
        strncpy(outBuf, "Not selected", maxLen - 1);
        outBuf[maxLen - 1] = '\0';
    }
}
