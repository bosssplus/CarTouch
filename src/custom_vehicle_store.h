/**
 * custom_vehicle_store.h - Persistent storage for custom vehicle profiles
 *
 * Part of CarTouch v2.0 (see CarTouch_SPEC.md section 6).
 *
 * Unlike AppConfig, which is a fixed struct in NVS, custom (Learned/
 * Manual) profiles are stored as separate JSON files in SPIFFS, since
 * their count and command sizes vary:
 *
 *   /custom_vehicles/index.json      - summary list (for quick display)
 *   /custom_vehicles/profile_0.json  - full details of profile 0
 *   /custom_vehicles/profile_1.json  - full details of profile 1
 *   ...
 *
 * Uses ArduinoJson, already a project dependency
 * (bblanchon/ArduinoJson in platformio.ini) - no new library added.
 */

#ifndef CUSTOM_VEHICLE_STORE_H
#define CUSTOM_VEHICLE_STORE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "custom_vehicle.h"

class CustomVehicleStore {
public:
    CustomVehicleStore();

    /**
     * Creates /custom_vehicles if missing and loads the index from
     * SPIFFS. Must be called after SPIFFS.begin() and before any other
     * method.
     */
    bool begin();

    /** Number of custom profiles currently in use (inUse=true slots). */
    uint8_t getProfileCount();

    /**
     * Reads a profile's summary by index (no full commands - fast).
     * Used for list display.
     * @return true if the slot is occupied
     */
    bool getProfileSummary(uint8_t index, CustomVehicleProfile& outSummary);

    /** Reads a full profile (including all commands) from profile_N.json. */
    bool loadProfile(uint8_t index, CustomVehicleProfile& outProfile);

    /** Saves a full profile (create or overwrite); also updates the index. */
    bool saveProfile(const CustomVehicleProfile& profile);

    /**
     * Creates a new empty profile and reserves a free slot.
     * @param name     user-chosen name
     * @param outIndex [out] the reserved slot index
     * @return true if a free slot was available
     */
    bool createNewProfile(const char* name, const char* brand,
                          const char* model, uint16_t year, uint8_t& outIndex);

    /** Deletes a profile entirely (file + index entry). */
    bool deleteProfile(uint8_t index);

    /**
     * Adds or updates a command within an existing profile (if the
     * label already exists, it's overwritten - used for relearning).
     * @return true on success (there was room)
     */
    bool upsertCommand(uint8_t profileIndex, const LearnedCommand& cmd);

    /** Changes a command's status (used after verify_confirm). */
    bool setCommandStatus(uint8_t profileIndex, const char* label,
                          CommandStatus newStatus, bool incrementFailCount = false);

    /** Finds a specific command within a profile by label. */
    bool findCommand(uint8_t profileIndex, const char* label, LearnedCommand& outCmd);

    /** Exports a profile as a raw JSON string (for download/export). */
    bool exportProfileJSON(uint8_t index, String& outJson);

    /**
     * Imports a profile from a JSON string (for upload/import). Per the
     * security policy in SPEC section 6.3: every imported command is
     * downgraded to CMD_UNVERIFIED regardless of the status in the
     * source file, since this device has not tested these commands on
     * this specific vehicle.
     * @param outIndex [out] the new slot index
     * @return true on success (valid JSON and a free slot existed)
     */
    bool importProfileJSON(const String& json, uint8_t& outIndex);

private:
    bool _initialized;

    // In-RAM cache of summaries only (not full commands) so listing is
    // fast without re-reading flash every time.
    CustomVehicleProfile _summaryCache[MAX_CUSTOM_VEHICLES];

    String _profilePath(uint8_t index);
    bool    _loadIndex();
    bool     _saveIndex();
    bool      _writeProfileFile(const CustomVehicleProfile& profile);
    bool       _readProfileFile(uint8_t index, CustomVehicleProfile& outProfile);

    void _profileToJson(const CustomVehicleProfile& profile, JsonDocument& doc);
    bool _jsonToProfile(JsonDocument& doc, CustomVehicleProfile& profile);
};

#endif // CUSTOM_VEHICLE_STORE_H
