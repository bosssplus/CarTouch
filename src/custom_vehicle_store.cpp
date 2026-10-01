/**
 * custom_vehicle_store.cpp - Custom profile storage implementation
 */

#include "custom_vehicle_store.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "ct_json_validation.h"

#define CUSTOM_VEHICLES_DIR "/custom_vehicles"
#define INDEX_FILE_PATH     "/custom_vehicles/index.json"

// SPIFFS (default CONFIG_SPIFFS_OBJ_NAME_LEN = 32) rejects any path longer than
// 31 characters, including the leading '/'. Every profile file is also written
// as "<path>.tmp" / "<path>.bak" by _atomicWriteJSON(), so the *longest* derived
// name must still fit. (The legacy "profile_N.json" naming made ".tmp"/".bak"
// 35 characters long, so every profile save failed on real hardware.)
#define CVS_PROFILE_PREFIX         "/custom_vehicles/p"
#define CVS_LEGACY_PROFILE_PREFIX  "/custom_vehicles/profile_"
#define CVS_SPIFFS_MAX_PATH        31

static_assert(MAX_CUSTOM_VEHICLES <= 10,
              "Profile file names assume a single-digit slot index");
static_assert((sizeof(CVS_PROFILE_PREFIX) - 1) + 1 + (sizeof(".json") - 1) +
              (sizeof(".tmp") - 1) <= CVS_SPIFFS_MAX_PATH,
              "Profile .tmp/.bak file name exceeds the SPIFFS 31-character path limit");
static_assert((sizeof(INDEX_FILE_PATH) - 1) + (sizeof(".tmp") - 1) <= CVS_SPIFFS_MAX_PATH,
              "Index .tmp/.bak file name exceeds the SPIFFS 31-character path limit");

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Constructor
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

CustomVehicleStore::CustomVehicleStore() {
    _initialized = false;
    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        _summaryCache[i].inUse = false;
    }
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Init
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CustomVehicleStore::begin() {
    if (!SPIFFS.exists(CUSTOM_VEHICLES_DIR)) {
        // SPIFFS on ESP32 has no real directories (it's flat), but the
        // path prefix is kept for logical consistency and readability.
        // No mkdir needed since SPIFFS.open works with the full path.
    }

    // One-time migration from the legacy "profile_N.json" names (31 chars, so
    // they could not be journaled) to the short "pN.json" names.
    for (uint8_t i = 0; i < MAX_CUSTOM_VEHICLES; ++i) {
        const String legacyPath = String(CVS_LEGACY_PROFILE_PREFIX) + String(i) + ".json";
        if (!SPIFFS.exists(legacyPath)) continue;
        if (SPIFFS.exists(_profilePath(i))) SPIFFS.remove(legacyPath);
        else SPIFFS.rename(legacyPath, _profilePath(i));
    }

    _recoverAtomicFile(INDEX_FILE_PATH);
    for (uint8_t i = 0; i < MAX_CUSTOM_VEHICLES; ++i) _recoverAtomicFile(_profilePath(i));

    bool result = _loadIndex();
    _initialized = result;

    Serial.printf("[CVS] CustomVehicleStore %s - %d profile(s) found\n",
                  result ? "ready" : "failed", getProfileCount());

    return result;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Profile file path
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

String CustomVehicleStore::_profilePath(uint8_t index) {
    return String(CVS_PROFILE_PREFIX) + String(index) + ".json";
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Index load / save
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CustomVehicleStore::_loadIndex() {
    if (!SPIFFS.exists(INDEX_FILE_PATH)) {
        // First run may legitimately have no profile files. If profile files
        // do exist, however, rebuild the index from the authoritative files
        // instead of silently hiding recoverable user data.
        if (_rebuildIndexFromProfiles()) {
            return true;
        }
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
        Serial.printf("[CVS] Failed to parse index: %s - rebuilding from profile files\n", err.c_str());
        return _rebuildIndexFromProfiles();
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
        _summaryCache[idx].revision        = item["revision"] | 0;
        _summaryCache[idx].commandCount       = item["commandCount"] | 0;
    }

    return true;
}

bool CustomVehicleStore::_rebuildIndexFromProfiles() {
    bool foundAny = false;
    for (uint8_t i = 0; i < MAX_CUSTOM_VEHICLES; ++i) {
        String path = _profilePath(i);
        if (!SPIFFS.exists(path)) continue;

        CustomVehicleProfile profile;
        if (!_readProfileFile(i, profile)) {
            Serial.printf("[CVS] Ignoring invalid profile file %s during index rebuild\n", path.c_str());
            continue;
        }
        if (profile.id != i) {
            Serial.printf("[CVS] Ignoring profile %s: embedded id %u does not match slot %u\n",
                          path.c_str(), (unsigned)profile.id, (unsigned)i);
            continue;
        }

        _summaryCache[i] = profile;
        _summaryCache[i].inUse = true;
        foundAny = true;
    }

    if (foundAny) {
        // Recreate the derived index only after all authoritative profile files
        // have been validated. If this write fails, the profiles themselves
        // remain intact and the next boot can rebuild again.
        if (!_saveIndex()) {
            Serial.println("[CVS] Profile recovery succeeded but index rewrite failed");
        }
    }
    return foundAny;
}

bool CustomVehicleStore::_atomicWriteJSON(const String& path, JsonDocument& doc) {
    const String tmpPath = path + ".tmp";
    const String bakPath = path + ".bak";

    // Journal the previous committed file before promoting the new file.
    // If power fails between these operations, begin() can recover the
    // backup or a validated temporary JSON instead of losing the profile.
    SPIFFS.remove(tmpPath);
    File tmpFile = SPIFFS.open(tmpPath, "w");
    if (!tmpFile) return false;
    const size_t written = serializeJson(doc, tmpFile);
    tmpFile.flush();
    tmpFile.close();
    if (written == 0) { SPIFFS.remove(tmpPath); return false; }

    SPIFFS.remove(bakPath);
    if (SPIFFS.exists(path) && !SPIFFS.rename(path, bakPath)) {
        SPIFFS.remove(tmpPath);
        return false;
    }
    if (!SPIFFS.rename(tmpPath, path)) {
        if (SPIFFS.exists(bakPath)) SPIFFS.rename(bakPath, path);
        return false;
    }
    SPIFFS.remove(bakPath);
    return true;
}

void CustomVehicleStore::_recoverAtomicFile(const String& path) {
    const String tmpPath = path + ".tmp";
    const String bakPath = path + ".bak";
    if (SPIFFS.exists(path)) { SPIFFS.remove(tmpPath); SPIFFS.remove(bakPath); return; }
    if (SPIFFS.exists(bakPath) && SPIFFS.rename(bakPath, path)) {
        SPIFFS.remove(tmpPath);
        return;
    }
    if (SPIFFS.exists(tmpPath)) {
        File tmp = SPIFFS.open(tmpPath, "r");
        bool valid = false;
        if (tmp) { JsonDocument check; valid = !deserializeJson(check, tmp); tmp.close(); }
        if (valid && SPIFFS.rename(tmpPath, path)) return;
        SPIFFS.remove(tmpPath);
    }
    SPIFFS.remove(bakPath);
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
        item["revision"]                       = _summaryCache[i].revision;
        item["commandCount"]                     = _summaryCache[i].commandCount;
    }

    return _atomicWriteJSON(INDEX_FILE_PATH, doc);
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Profile <-> JSON conversion
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

void CustomVehicleStore::_profileToJson(const CustomVehicleProfile& profile, JsonDocument& doc) {
    doc["id"]      = profile.id;
    doc["name"]       = profile.name;
    doc["brand"]          = profile.brand;
    doc["model"]              = profile.model;
    doc["year"]                   = profile.year;
    doc["revision"]               = profile.revision;

    JsonArray cmds = doc["commands"].to<JsonArray>();
    for (int i = 0; i < profile.commandCount; i++) {
        const LearnedCommand& c    = profile.commands[i];
        JsonObject            item = cmds.add<JsonObject>();
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

bool CustomVehicleStore::_jsonToProfile(JsonDocument& doc, CustomVehicleProfile& profile,
                                        bool strict) {
    const char* nameStr  = doc["name"]  | "";
    const char* brandStr = doc["brand"] | "";
    const char* modelStr = doc["model"] | "";
    int         yearVal  = doc["year"]  | 0;
    uint32_t    revisionVal = doc["revision"] | 0;

    if (strict) {
        if (strlen(nameStr) == 0 || strlen(nameStr) >= sizeof(profile.name)) {
            Serial.println("[CVS] Reject import: 'name' missing or too long"); return false;
        }
        if (strlen(brandStr) >= sizeof(profile.brand) ||
            strlen(modelStr) >= sizeof(profile.model)) {
            Serial.println("[CVS] Reject import: 'brand'/'model' too long"); return false;
        }
        if (yearVal < 0 || yearVal > 9999) {
            Serial.println("[CVS] Reject import: 'year' out of range"); return false;
        }
    }

    profile.id = doc["id"] | 0;
    memset(profile.name,  0, sizeof(profile.name));
    memset(profile.brand, 0, sizeof(profile.brand));
    memset(profile.model, 0, sizeof(profile.model));
    strncpy(profile.name,  nameStr,  sizeof(profile.name)  - 1);
    strncpy(profile.brand, brandStr, sizeof(profile.brand) - 1);
    strncpy(profile.model, modelStr, sizeof(profile.model) - 1);
    profile.year  = (uint16_t)yearVal;
    profile.revision = revisionVal;
    profile.inUse = true;
    profile.commandCount = 0;

    if (strict && !doc["commands"].is<JsonArray>()) {
        Serial.println("[CVS] Reject import: 'commands' array missing or not an array");
        return false;
    }
    JsonArray cmds = doc["commands"].as<JsonArray>();

    if (strict && cmds.size() > MAX_LEARNED_COMMANDS_PER_VEHICLE) {
        Serial.printf("[CVS] Reject import: %u commands exceed cap %u\n",
                      (unsigned)cmds.size(), (unsigned)MAX_LEARNED_COMMANDS_PER_VEHICLE);
        return false;
    }

    for (JsonObject item : cmds) {
        if (profile.commandCount >= MAX_LEARNED_COMMANDS_PER_VEHICLE) {
            Serial.println("[CVS] Command count exceeded the cap - remaining skipped");
            break;
        }
        const char* labelStr = item["label"] | "";
        CtJsonCommandFields validatedFields;
        if (strict && !ctValidateImportedCommand(item, validatedFields)) {
            Serial.println("[CVS] Reject import: command fields failed strict validation");
            return false;
        }
        if (strict) {
            if (strlen(labelStr) == 0) {
                Serial.println("[CVS] Reject import: empty 'label'"); return false;
            }
            if (strlen(labelStr) >= sizeof(profile.commands[0].label)) {
                Serial.println("[CVS] Reject import: 'label' too long"); return false;
            }
            for (uint8_t k = 0; k < profile.commandCount; k++) {
                if (strcmp(profile.commands[k].label, labelStr) == 0) {
                    Serial.printf("[CVS] Reject import: duplicate label '%s'\n", labelStr);
                    return false;
                }
            }
        }

        if (strict) {
            if (!item["canId"].is<int>()) {
                Serial.printf("[CVS] Reject import: 'canId' must be an integer for '%s'\n", labelStr);
                return false;
            }
            if (!item["extended"].is<bool>()) {
                Serial.printf("[CVS] Reject import: 'extended' must be boolean for '%s'\n", labelStr);
                return false;
            }
            if (!item["length"].is<int>()) {
                Serial.printf("[CVS] Reject import: 'length' must be an integer for '%s'\n", labelStr);
                return false;
            }
        }

        int canIdRaw = item["canId"] | -1;
        uint32_t canIdVal = (uint32_t)canIdRaw;
        bool extendedVal = item["extended"] | false;
        if (strict && (canIdRaw < 0 || canIdVal > (extendedVal ? 0x1FFFFFFFu : 0x7FFu))) {
            Serial.printf("[CVS] Reject import: 'canId' out of range for '%s'\n", labelStr);
            return false;
        }

        int lenVal = item["length"] | (strict ? -1 : 0);
        if (strict && (lenVal < 0 || lenVal > 8)) {
            Serial.printf("[CVS] Reject import: 'length' out of range for '%s'\n", labelStr);
            return false;
        }
        if (!strict && lenVal > 8) lenVal = 8;

        JsonArray dataArr = item["data"].as<JsonArray>();
        if (strict) {
            if (item["data"].isNull()) {
                if (lenVal > 0) {
                    Serial.printf("[CVS] Reject import: missing 'data' for '%s'\n", labelStr);
                    return false;
                }
            } else if (!item["data"].is<JsonArray>()) {
                Serial.printf("[CVS] Reject import: 'data' not an array for '%s'\n", labelStr);
                return false;
            } else if ((int)dataArr.size() != lenVal) {
                Serial.printf("[CVS] Reject import: 'data' size %u != length %d for '%s'\n",
                              (unsigned)dataArr.size(), lenVal, labelStr);
                return false;
            }
            if (dataArr.size() > 8) {
                Serial.printf("[CVS] Reject import: 'data' >8 for '%s'\n", labelStr);
                return false;
            }
            for (JsonVariant v : dataArr) {
                if (!v.is<int>() || v.as<int>() < 0 || v.as<int>() > 255) {
                    Serial.printf("[CVS] Reject import: invalid byte for '%s'\n", labelStr);
                    return false;
                }
            }
        }

        const char* displayNameStr = item["displayName"] | "";
        if (strict && strlen(displayNameStr) >= sizeof(profile.commands[0].displayName)) {
            Serial.printf("[CVS] Reject import: 'displayName' too long for '%s'\n", labelStr);
            return false;
        }

        if (strict && !item["source"].is<int>()) {
            Serial.printf("[CVS] Reject import: 'source' must be an integer for '%s'\n", labelStr);
            return false;
        }
        int sourceVal = item["source"] | (int)SOURCE_MANUAL;
        if (strict && (sourceVal < (int)SOURCE_DBC || sourceVal > (int)SOURCE_MANUAL)) {
            Serial.printf("[CVS] Reject import: invalid 'source' for '%s'\n", labelStr);
            return false;
        }

        const char* statusStr = item["status"] | "unverified";
        if (strict && !item["status"].is<const char*>()) {
            Serial.printf("[CVS] Reject import: 'status' must be a string for '%s'\n", labelStr);
            return false;
        }
        if (strict && strcmp(statusStr, "verified") != 0 &&
            strcmp(statusStr, "unverified") != 0) {
            Serial.printf("[CVS] Reject import: unknown 'status' for '%s'\n", labelStr);
            return false;
        }

        LearnedCommand& cc = profile.commands[profile.commandCount];
        memset(&cc, 0, sizeof(cc));
        strncpy(cc.label, labelStr, sizeof(cc.label) - 1);
        strncpy(cc.displayName, displayNameStr, sizeof(cc.displayName) - 1);
        cc.canId      = canIdVal;
        cc.isExtended = extendedVal;
        cc.length     = (uint8_t)lenVal;

        int bi = 0;
        for (JsonVariant v : dataArr) { if (bi >= 8) break; cc.data[bi++] = v.as<uint8_t>(); }

        cc.source = (CommandSource)sourceVal;
        // Imported commands are never trusted as VERIFIED on this device.
        // The status field is validated above only to reject malformed input.
        cc.status = CMD_UNVERIFIED;

        cc.timesObserved = item["timesObserved"] | 0;
        cc.failCount     = item["failCount"] | 0;
        cc.createdAt     = item["createdAt"] | 0;
        profile.commandCount++;
    }

    if (strict && profile.commandCount == 0) {
        Serial.println("[CVS] Reject import: profile contains no commands"); return false;
    }
    return true;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Profile file I/O
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

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
    return _atomicWriteJSON(path, doc);
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Profile count / summary
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

uint8_t CustomVehicleStore::getProfileCount() {
    if (!_initialized) return 0;
    uint8_t count = 0;
    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        if (_summaryCache[i].inUse) count++;
    }
    return count;
}

bool CustomVehicleStore::getProfileSummary(uint8_t index, CustomVehicleProfile& outSummary) {
    if (!_initialized || index >= MAX_CUSTOM_VEHICLES || !_summaryCache[index].inUse) return false;
    outSummary = _summaryCache[index];
    return true;
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Load / save
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

bool CustomVehicleStore::loadProfile(uint8_t index, CustomVehicleProfile& outProfile) {
    if (!_initialized || index >= MAX_CUSTOM_VEHICLES || !_summaryCache[index].inUse) return false;
    if (!_readProfileFile(index, outProfile)) return false;
    // Keep the summary generation synchronized with the authoritative profile
    // file, including profiles created before the revision field existed.
    _summaryCache[index].revision = outProfile.revision;
    return true;
}

bool CustomVehicleStore::saveProfile(const CustomVehicleProfile& profile) {
    if (!_initialized || profile.id >= MAX_CUSTOM_VEHICLES) return false;

    CustomVehicleProfile toSave = profile;
    uint32_t nextRevision = _summaryCache[profile.id].inUse
        ? (_summaryCache[profile.id].revision + 1u)
        : 1u;
    if (nextRevision == 0) nextRevision = 1;
    toSave.revision = nextRevision;

    if (!_writeProfileFile(toSave)) return false;

    // Update the summary cache + index
    _summaryCache[toSave.id]         = toSave;
    _summaryCache[toSave.id].inUse   = true;

    return _saveIndex();
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Create new profile
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CustomVehicleStore::createNewProfile(const char* name, const char* brand,
                                          const char* model, uint16_t year,
                                          uint8_t& outIndex) {
    if (!_initialized) return false;
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
    strncpy(profile.name, name ? name : "New Vehicle", sizeof(profile.name) - 1);
    strncpy(profile.brand, brand ? brand : "", sizeof(profile.brand) - 1);
    strncpy(profile.model, model ? model : "", sizeof(profile.model) - 1);
    profile.year          = year;
    profile.commandCount     = 0;

    if (!saveProfile(profile)) return false;

    outIndex = (uint8_t)freeSlot;
    Serial.printf("[CVS] New profile created: %s (slot %d)\n", profile.name, freeSlot);
    return true;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Delete
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CustomVehicleStore::deleteProfile(uint8_t index) {
    if (!_initialized || index >= MAX_CUSTOM_VEHICLES || !_summaryCache[index].inUse) return false;

    String path = _profilePath(index);
    if (SPIFFS.exists(path)) {
        SPIFFS.remove(path);
    }

    _summaryCache[index].inUse = false;
    memset(&_summaryCache[index], 0, sizeof(CustomVehicleProfile));

    return _saveIndex();
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Add / update a command
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

bool CustomVehicleStore::upsertCommand(uint8_t profileIndex, const LearnedCommand& cmd) {
    if (!_initialized) return false;
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

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Change command status
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CustomVehicleStore::setCommandStatus(uint8_t profileIndex, const char* label,
                                          CommandStatus newStatus, bool incrementFailCount) {
    if (!_initialized) return false;
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

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Find a command
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CustomVehicleStore::findCommand(uint8_t profileIndex, const char* label, LearnedCommand& outCmd) {
    if (!_initialized) return false;
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

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Export
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CustomVehicleStore::exportProfileJSON(uint8_t index, String& outJson) {
    if (!_initialized) return false;
    CustomVehicleProfile profile;
    if (!loadProfile(index, profile)) return false;

    JsonDocument doc;
    _profileToJson(profile, doc);

    outJson = "";
    serializeJson(doc, outJson);
    return outJson.length() > 0;
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Import
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

bool CustomVehicleStore::importProfileJSON(const String& json, uint8_t& outIndex) {
    if (!_initialized) return false;
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
    if (!_jsonToProfile(doc, profile, /*strict=*/true)) {
        Serial.println("[CVS] Import rejected - strict validation failed");
        return false;
    }

    profile.id     = (uint8_t)freeSlot;
    profile.inUse    = true;

    // Critical security policy (see CarTouch_SPEC.md): regardless of the
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
