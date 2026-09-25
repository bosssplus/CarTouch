/**
 * custom_vehicle_store.cpp - Custom profile storage implementation
 */

#include "custom_vehicle_store.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

#define CUSTOM_VEHICLES_DIR   "/custom_vehicles"
#define INDEX_FILE_PATH       "/custom_vehicles/index.json"

// ============================================================================
// Constructor
// ============================================================================

CustomVehicleStore::CustomVehicleStore() {
    _initialized = false;
    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        _summaryCache[i].inUse = false;
    }
}

// ============================================================================
// Init
// ============================================================================

bool CustomVehicleStore::begin() {
    if (!SPIFFS.exists(CUSTOM_VEHICLES_DIR)) {
        // SPIFFS on ESP32 has no real directories (it's flat), but the
        // path prefix is kept for logical consistency and readability.
        // No mkdir needed since SPIFFS.open works with the full path.
    }

    bool result = _loadIndex();
    _initialized = true;

    Serial.printf("[CVS] CustomVehicleStore ready - %d profile(s) found\n",
                  getProfileCount());

    return result;
}

// ============================================================================
// Profile file path
// ============================================================================

String CustomVehicleStore::_profilePath(uint8_t index) {
    return String(CUSTOM_VEHICLES_DIR) + "/profile_" + String(index) + ".json";
}

// ============================================================================
// Index load / save
// ============================================================================

bool CustomVehicleStore::_loadIndex() {
    if (!SPIFFS.exists(INDEX_FILE_PATH)) {
        // First run - no profiles exist yet. Not an error.
        Serial.println("[CVS] Index file not found - starting with an empty list");
        return true;
    }

    File file = SPIFFS.open(INDEX_FILE_PATH, "r");
    if (!file) {
        Serial.println("[CVS] Failed to open index file");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.printf("[CVS] Failed to parse index: %s\n", err.c_str());
        return false;
    }

    JsonArray arr = doc["profiles"].as<JsonArray>();
    for (JsonObject item : arr) {
        uint8_t idx = item["id"] | 0;
        if (idx >= MAX_CUSTOM_VEHICLES) continue;

        _summaryCache[idx].id      = idx;
        _summaryCache[idx].inUse    = true;
        strncpy(_summaryCache[idx].name,  item["name"]  | "", sizeof(_summaryCache[idx].name) - 1);
        strncpy(_summaryCache[idx].brand, item["brand"] | "", sizeof(_summaryCache[idx].brand) - 1);
        strncpy(_summaryCache[idx].model, item["model"] | "", sizeof(_summaryCache[idx].model) - 1);
        _summaryCache[idx].year            = item["year"] | 0;
        _summaryCache[idx].commandCount       = item["commandCount"] | 0;
    }

    return true;
}

bool CustomVehicleStore::_saveIndex() {
    JsonDocument doc;
    JsonArray arr = doc["profiles"].to<JsonArray>();

    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        if (!_summaryCache[i].inUse) continue;

        JsonObject item = arr.add<JsonObject>();
        item["id"]              = _summaryCache[i].id;
        item["name"]               = _summaryCache[i].name;
        item["brand"]                  = _summaryCache[i].brand;
        item["model"]                     = _summaryCache[i].model;
        item["year"]                          = _summaryCache[i].year;
        item["commandCount"]                     = _summaryCache[i].commandCount;
    }

    File file = SPIFFS.open(INDEX_FILE_PATH, "w");
    if (!file) {
        Serial.println("[CVS] Failed to open index file for writing");
        return false;
    }

    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

// ============================================================================
// Profile <-> JSON conversion
// ============================================================================

void CustomVehicleStore::_profileToJson(const CustomVehicleProfile& profile, JsonDocument& doc) {
    doc["id"]      = profile.id;
    doc["name"]       = profile.name;
    doc["brand"]          = profile.brand;
    doc["model"]              = profile.model;
    doc["year"]                   = profile.year;

    JsonArray cmds = doc["commands"].to<JsonArray>();
    for (int i = 0; i < profile.commandCount; i++) {
        const LearnedCommand& c = profile.commands[i];
        JsonObject item = cmds.add<JsonObject>();
        item["label"]           = c.label;
        item["displayName"]        = c.displayName;
        item["canId"]                  = c.canId;
        item["extended"]                   = c.isExtended;
        item["length"]                          = c.length;

        JsonArray dataArr = item["data"].to<JsonArray>();
        for (int b = 0; b < c.length && b < 8; b++) {
            dataArr.add(c.data[b]);
        }

        item["source"]           = (int)c.source;
        item["status"]              = (c.status == CMD_VERIFIED) ? "verified" : "unverified";
        item["timesObserved"]           = c.timesObserved;
        item["failCount"]                  = c.failCount;
        item["createdAt"]                     = c.createdAt;
    }
}

bool CustomVehicleStore::_jsonToProfile(JsonDocument& doc, CustomVehicleProfile& profile) {
    profile.id = doc["id"] | 0;
    strncpy(profile.name,  doc["name"]  | "", sizeof(profile.name) - 1);
    strncpy(profile.brand, doc["brand"] | "", sizeof(profile.brand) - 1);
    strncpy(profile.model, doc["model"] | "", sizeof(profile.model) - 1);
    profile.year  = doc["year"] | 0;
    profile.inUse = true;

    profile.commandCount = 0;
    JsonArray cmds = doc["commands"].as<JsonArray>();
    for (JsonObject item : cmds) {
        if (profile.commandCount >= MAX_LEARNED_COMMANDS_PER_VEHICLE) {
            Serial.println("[CVS] Command count exceeded the cap - remaining entries skipped");
            break;
        }

        LearnedCommand& c = profile.commands[profile.commandCount];
        strncpy(c.label, item["label"] | "", sizeof(c.label) - 1);
        strncpy(c.displayName, item["displayName"] | "", sizeof(c.displayName) - 1);
        c.canId       = item["canId"] | 0;
        c.isExtended    = item["extended"] | false;
        c.length          = item["length"] | 0;
        if (c.length > 8) c.length = 8;

        JsonArray dataArr = item["data"].as<JsonArray>();
        int b = 0;
        for (JsonVariant v : dataArr) {
            if (b >= 8) break;
            c.data[b++] = v.as<uint8_t>();
        }

        c.source = (CommandSource)(item["source"] | (int)SOURCE_MANUAL);

        const char* statusStr = item["status"] | "unverified";
        c.status = (strcmp(statusStr, "verified") == 0) ? CMD_VERIFIED : CMD_UNVERIFIED;

        c.timesObserved = item["timesObserved"] | 0;
        c.failCount        = item["failCount"] | 0;
        c.createdAt            = item["createdAt"] | 0;

        profile.commandCount++;
    }

    return true;
}

// ============================================================================
// Profile file I/O
// ============================================================================

bool CustomVehicleStore::_readProfileFile(uint8_t index, CustomVehicleProfile& outProfile) {
    String path = _profilePath(index);
    if (!SPIFFS.exists(path)) {
        Serial.printf("[CVS] Profile file not found: %s\n", path.c_str());
        return false;
    }

    File file = SPIFFS.open(path, "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.printf("[CVS] Failed to parse profile %d: %s\n", index, err.c_str());
        return false;
    }

    return _jsonToProfile(doc, outProfile);
}

bool CustomVehicleStore::_writeProfileFile(const CustomVehicleProfile& profile) {
    JsonDocument doc;
    _profileToJson(profile, doc);

    String path = _profilePath(profile.id);
    File file = SPIFFS.open(path, "w");
    if (!file) {
        Serial.printf("[CVS] Failed to open %s for writing\n", path.c_str());
        return false;
    }

    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

// ============================================================================
// Profile count / summary
// ============================================================================

uint8_t CustomVehicleStore::getProfileCount() {
    uint8_t count = 0;
    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        if (_summaryCache[i].inUse) count++;
    }
    return count;
}

bool CustomVehicleStore::getProfileSummary(uint8_t index, CustomVehicleProfile& outSummary) {
    if (index >= MAX_CUSTOM_VEHICLES || !_summaryCache[index].inUse) return false;
    outSummary = _summaryCache[index];
    return true;
}

// ============================================================================
// Load / save
// ============================================================================

bool CustomVehicleStore::loadProfile(uint8_t index, CustomVehicleProfile& outProfile) {
    if (index >= MAX_CUSTOM_VEHICLES || !_summaryCache[index].inUse) return false;
    return _readProfileFile(index, outProfile);
}

bool CustomVehicleStore::saveProfile(const CustomVehicleProfile& profile) {
    if (profile.id >= MAX_CUSTOM_VEHICLES) return false;

    if (!_writeProfileFile(profile)) return false;

    // Update the summary cache + index
    _summaryCache[profile.id]         = profile;
    _summaryCache[profile.id].inUse   = true;

    return _saveIndex();
}

// ============================================================================
// Create new profile
// ============================================================================

bool CustomVehicleStore::createNewProfile(const char* name, const char* brand,
                                          const char* model, uint16_t year,
                                          uint8_t& outIndex) {
    int freeSlot = -1;
    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        if (!_summaryCache[i].inUse) {
            freeSlot = i;
            break;
        }
    }

    if (freeSlot < 0) {
        Serial.printf("[CVS] Custom profile capacity full (max %d)\n",
                     (int)MAX_CUSTOM_VEHICLES);
        return false;
    }

    CustomVehicleProfile profile;
    profile.id = (uint8_t)freeSlot;
    profile.inUse = true;
    strncpy(profile.name, name ? name : "خودروی جدید", sizeof(profile.name) - 1);
    strncpy(profile.brand, brand ? brand : "", sizeof(profile.brand) - 1);
    strncpy(profile.model, model ? model : "", sizeof(profile.model) - 1);
    profile.year          = year;
    profile.commandCount     = 0;

    if (!saveProfile(profile)) return false;

    outIndex = (uint8_t)freeSlot;
    Serial.printf("[CVS] New profile created: %s (slot %d)\n", profile.name, freeSlot);
    return true;
}

// ============================================================================
// Delete
// ============================================================================

bool CustomVehicleStore::deleteProfile(uint8_t index) {
    if (index >= MAX_CUSTOM_VEHICLES || !_summaryCache[index].inUse) return false;

    String path = _profilePath(index);
    if (SPIFFS.exists(path)) {
        SPIFFS.remove(path);
    }

    _summaryCache[index].inUse = false;
    memset(&_summaryCache[index], 0, sizeof(CustomVehicleProfile));

    return _saveIndex();
}

// ============================================================================
// Add / update a command
// ============================================================================

bool CustomVehicleStore::upsertCommand(uint8_t profileIndex, const LearnedCommand& cmd) {
    if (profileIndex >= MAX_CUSTOM_VEHICLES || !_summaryCache[profileIndex].inUse) {
        Serial.println("[CVS] Target profile does not exist");
        return false;
    }

    CustomVehicleProfile profile;
    if (!loadProfile(profileIndex, profile)) return false;

    // Check whether a command with this label already exists (relearning)
    int existingIdx = -1;
    for (int i = 0; i < profile.commandCount; i++) {
        if (strcmp(profile.commands[i].label, cmd.label) == 0) {
            existingIdx = i;
            break;
        }
    }

    if (existingIdx >= 0) {
        profile.commands[existingIdx] = cmd;
        Serial.printf("[CVS] Command '%s' overwritten\n", cmd.label);
    } else {
        if (profile.commandCount >= MAX_LEARNED_COMMANDS_PER_VEHICLE) {
            Serial.println("[CVS] This profile's command capacity is full");
            return false;
        }
        profile.commands[profile.commandCount] = cmd;
        profile.commandCount++;
        Serial.printf("[CVS] New command '%s' added\n", cmd.label);
    }

    return saveProfile(profile);
}

// ============================================================================
// Change command status
// ============================================================================

bool CustomVehicleStore::setCommandStatus(uint8_t profileIndex, const char* label,
                                          CommandStatus newStatus, bool incrementFailCount) {
    CustomVehicleProfile profile;
    if (!loadProfile(profileIndex, profile)) return false;

    for (int i = 0; i < profile.commandCount; i++) {
        if (strcmp(profile.commands[i].label, label) == 0) {
            profile.commands[i].status = newStatus;
            if (incrementFailCount) {
                profile.commands[i].failCount++;
            }
            return saveProfile(profile);
        }
    }

    Serial.printf("[CVS] Command '%s' not found for status update\n", label);
    return false;
}

// ============================================================================
// Find a command
// ============================================================================

bool CustomVehicleStore::findCommand(uint8_t profileIndex, const char* label, LearnedCommand& outCmd) {
    CustomVehicleProfile profile;
    if (!loadProfile(profileIndex, profile)) return false;

    for (int i = 0; i < profile.commandCount; i++) {
        if (strcmp(profile.commands[i].label, label) == 0) {
            outCmd = profile.commands[i];
            return true;
        }
    }

    return false;
}

// ============================================================================
// Export
// ============================================================================

bool CustomVehicleStore::exportProfileJSON(uint8_t index, String& outJson) {
    CustomVehicleProfile profile;
    if (!loadProfile(index, profile)) return false;

    JsonDocument doc;
    _profileToJson(profile, doc);

    outJson = "";
    serializeJson(doc, outJson);
    return outJson.length() > 0;
}

// ============================================================================
// Import
// ============================================================================

bool CustomVehicleStore::importProfileJSON(const String& json, uint8_t& outIndex) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[CVS] Imported JSON is invalid: %s\n", err.c_str());
        return false;
    }

    // Minimal structural validation
    if (!doc["name"].is<const char*>() && !doc["name"].is<String>()) {
        Serial.println("[CVS] 'name' field missing from imported JSON");
        return false;
    }

    int freeSlot = -1;
    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        if (!_summaryCache[i].inUse) {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot < 0) {
        Serial.println("[CVS] Profile capacity full - import not possible");
        return false;
    }

    CustomVehicleProfile profile;
    if (!_jsonToProfile(doc, profile)) return false;

    profile.id     = (uint8_t)freeSlot;
    profile.inUse    = true;

    // Critical security policy (SPEC section 6.3): regardless of the
    // verified/unverified status in the source file, every imported
    // command is downgraded to UNVERIFIED - this device has not tested
    // these specific commands on this specific vehicle, even if they
    // were verified on a different device/vehicle.
    for (int i = 0; i < profile.commandCount; i++) {
        profile.commands[i].status      = CMD_UNVERIFIED;
        profile.commands[i].failCount      = 0;
    }

    if (!saveProfile(profile)) return false;

    outIndex = (uint8_t)freeSlot;
    Serial.printf("[CVS] Profile '%s' imported (slot %d) - all commands marked UNVERIFIED\n",
                  profile.name, freeSlot);
    return true;
}
