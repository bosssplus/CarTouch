/**
 * active_profile_manager.cpp - Command-source unification layer implementation
 */

#include "active_profile_manager.h"
#include "custom_vehicle.h"

// ============================================================================
// Constructor
// ============================================================================

ActiveProfileManager::ActiveProfileManager(VehicleDB& vehicleDB, CustomVehicleStore& customStore)
    : _vehicleDB(vehicleDB), _customStore(customStore) {
    _activeKind           = ACTIVE_KIND_NONE;
    _activeCustomIndex       = 0;
}

// ============================================================================
// Select a DBC vehicle
// ============================================================================

void ActiveProfileManager::selectDBCVehicle(const char* brand, const char* model) {
    _vehicleDB.setActiveVehicle(brand, model);
    _activeKind = ACTIVE_KIND_DBC;
    Serial.printf("[APM] Active vehicle (DBC): %s %s\n", brand, model);
}

// ============================================================================
// Select a custom vehicle
// ============================================================================

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

// ============================================================================
// Accessors
// ============================================================================

ActiveVehicleKind ActiveProfileManager::getActiveKind() {
    return _activeKind;
}

uint8_t ActiveProfileManager::getActiveCustomIndex() {
    return _activeCustomIndex;
}

// ============================================================================
// Command resolution
// ============================================================================

bool ActiveProfileManager::resolveCommand(const char* label, CanMessage& outMsg, String& outErrorReason) {
    if (_activeKind == ACTIVE_KIND_NONE) {
        outErrorReason = "هیچ خودرویی انتخاب نشده است";
        return false;
    }

    if (_activeKind == ACTIVE_KIND_DBC) {
        // -- DBC path ---------------------------------------------------------
        // Important note (per SPEC section 7.2): most DBC files bundled
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
        // implemented in this version - guessing at such a convention
        // without real data would be riskier than being honest about
        // its absence.
        outErrorReason = "این فایل DBC فرمان نوشتن برای این عملکرد ندارد - "
                         "از «حالت یادگیری» یا «ورود دستی» استفاده کنید";
        return false;
    }

    // -- Custom path (Learned / Manual) ----------------------------------------
    LearnedCommand cmd;
    if (!_customStore.findCommand(_activeCustomIndex, label, cmd)) {
        outErrorReason = "فرمانی با این برچسب برای این خودرو یادگرفته/ثبت نشده است";
        return false;
    }

    // Critical safety check - defense in depth. vehicle_control.cpp also
    // has this check, but it's repeated here too, so this class alone
    // (e.g. if called from somewhere else in the future) never returns
    // an unverified command as sendable.
    if (cmd.status != CMD_VERIFIED) {
        outErrorReason = "این فرمان هنوز تأیید نشده (UNVERIFIED) - "
                         "ابتدا از منوی «فرمان‌های من» تأیید کنید";
        return false;
    }

    outMsg.id          = cmd.canId;
    outMsg.isExtended    = cmd.isExtended;
    outMsg.isRemote        = false;
    outMsg.length             = cmd.length;
    memcpy(outMsg.data, cmd.data, cmd.length);

    return true;
}

// ============================================================================
// Active vehicle name
// ============================================================================

void ActiveProfileManager::getActiveVehicleName(char* outBuf, size_t maxLen) {
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
            strncpy(outBuf, "؟", maxLen - 1);
        }
    } else {
        strncpy(outBuf, "انتخاب نشده", maxLen - 1);
        outBuf[maxLen - 1] = '\0';
    }
}
