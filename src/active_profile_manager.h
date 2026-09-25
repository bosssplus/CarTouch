/**
 * active_profile_manager.h - Command-source unification layer
 *
 * Part of CarTouch v2.0 (see CarTouch_SPEC.md sections 3.1 and 7.1).
 *
 * Sits between two command sources:
 *   - VehicleDB (built-in DBC files - existing v1.0 functionality, unchanged)
 *   - CustomVehicleStore (Learned/Manual profiles - new in v2.0)
 *
 * vehicle_control.cpp no longer talks to VehicleDB or CustomVehicleStore
 * directly; it only asks this class "for the current vehicle, what is
 * the command for this label?" and gets back a ready-to-send CanMessage
 * (or an error).
 *
 * Safety note: this class is responsible for checking CMD_VERIFIED. If
 * the active source is a custom profile (Learned/Manual) and the
 * relevant command is still CMD_UNVERIFIED, resolveCommand() must
 * return false - regardless of whether the caller (vehicle_control)
 * also re-checks this itself (defense in depth).
 */

#ifndef ACTIVE_PROFILE_MANAGER_H
#define ACTIVE_PROFILE_MANAGER_H

#include <Arduino.h>
#include "vehicle_db.h"
#include "custom_vehicle_store.h"
#include "can_manager.h"

// Type of the currently active vehicle source
enum ActiveVehicleKind : uint8_t {
    ACTIVE_KIND_NONE   = 0,  // No vehicle selected yet
    ACTIVE_KIND_DBC    = 1,  // A built-in DBC profile is active
    ACTIVE_KIND_CUSTOM = 2   // A custom (Learned/Manual) profile is active
};

class ActiveProfileManager {
public:
    ActiveProfileManager(VehicleDB& vehicleDB, CustomVehicleStore& customStore);

    /**
     * Selects a built-in DBC vehicle as active (existing legacy path,
     * unchanged - a thin wrapper around VehicleDB::setActiveVehicle).
     */
    void selectDBCVehicle(const char* brand, const char* model);

    /**
     * Selects a custom (Learned/Manual) profile as active.
     * @param profileIndex index within CustomVehicleStore
     * @return true if the profile was valid
     */
    bool selectCustomVehicle(uint8_t profileIndex);

    ActiveVehicleKind getActiveKind();

    /**
     * Resolves a command label (e.g. "lock_all") into a sendable
     * CanMessage, based on the currently active source.
     *
     * @param label           command label (from the CMD_LABEL_* constants in custom_vehicle.h)
     * @param outMsg          [out] the ready-to-send message on success
     * @param outErrorReason  [out] on failure, a Persian reason string (shown in the UI)
     * @return true if the command was found and is allowed to execute
     *         (either sourced from DBC, or from a custom profile with
     *         status==CMD_VERIFIED)
     */
    bool resolveCommand(const char* label, CanMessage& outMsg, String& outErrorReason);

    /** Display name of the currently active vehicle (for UI display). */
    void getActiveVehicleName(char* outBuf, size_t maxLen);

    /** Index of the active custom profile (only valid if getActiveKind() == ACTIVE_KIND_CUSTOM). */
    uint8_t getActiveCustomIndex();

private:
    VehicleDB&              _vehicleDB;
    CustomVehicleStore&        _customStore;

    ActiveVehicleKind _activeKind;
    uint8_t             _activeCustomIndex;
};

#endif // ACTIVE_PROFILE_MANAGER_H
