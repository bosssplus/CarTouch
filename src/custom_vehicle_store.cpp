/**
 * custom_vehicle_store.cpp - پیاده‌سازی ذخیره‌سازی پروفایل‌های سفارشی
 */

#include "custom_vehicle_store.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

#define CUSTOM_VEHICLES_DIR   "/custom_vehicles"
#define INDEX_FILE_PATH       "/custom_vehicles/index.json"

// ======================== سازنده ========================

CustomVehicleStore::CustomVehicleStore() {
    _initialized = false;
    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        _summaryCache[i].inUse = false;
    }
}

// ======================== مقداردهی اولیه ========================

bool CustomVehicleStore::begin() {
    if (!SPIFFS.exists(CUSTOM_VEHICLES_DIR)) {
        // SPIFFS در ESP32 پوشه‌ی واقعی ندارد (فلت است)، ولی برای
        // سازگاری منطقی و خوانایی مسیرها همین پیشوند را نگه می‌داریم.
        // نیازی به mkdir نیست چون SPIFFS.open با مسیر کامل کار می‌کند.
    }
    
    bool result = _loadIndex();
    _initialized = true;
    
    Serial.printf("[CVS] CustomVehicleStore آماده شد - %d پروفایل موجود\n", 
                  getProfileCount());
    
    return result;
}

// ======================== مسیر فایل پروفایل ========================

String CustomVehicleStore::_profilePath(uint8_t index) {
    return String(CUSTOM_VEHICLES_DIR) + "/profile_" + String(index) + ".json";
}

// ======================== بارگذاری ایندکس ========================

bool CustomVehicleStore::_loadIndex() {
    if (!SPIFFS.exists(INDEX_FILE_PATH)) {
        // اولین اجرا - هیچ پروفایلی هنوز وجود ندارد. این خطا نیست.
        Serial.println("[CVS] فایل ایندکس یافت نشد - شروع با لیست خالی");
        return true;
    }
    
    File file = SPIFFS.open(INDEX_FILE_PATH, "r");
    if (!file) {
        Serial.println("⚠️ [CVS] خطا در باز کردن فایل ایندکس");
        return false;
    }
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    
    if (err) {
        Serial.printf("⚠️ [CVS] خطا در پارس ایندکس: %s\n", err.c_str());
        return false;
    }
    
    JsonArray arr = doc["profiles"].as<JsonArray>();
    for (JsonObject item : arr) {
        uint8_t idx = item["id"] | 0;
        if (idx >= MAX_CUSTOM_VEHICLES) continue;
        
        _summaryCache[idx].id = idx;
        _summaryCache[idx].inUse = true;
        strncpy(_summaryCache[idx].name, item["name"] | "", sizeof(_summaryCache[idx].name) - 1);
        strncpy(_summaryCache[idx].brand, item["brand"] | "", sizeof(_summaryCache[idx].brand) - 1);
        strncpy(_summaryCache[idx].model, item["model"] | "", sizeof(_summaryCache[idx].model) - 1);
        _summaryCache[idx].year = item["year"] | 0;
        _summaryCache[idx].commandCount = item["commandCount"] | 0;
    }
    
    return true;
}

// ======================== ذخیره ایندکس ========================

bool CustomVehicleStore::_saveIndex() {
    JsonDocument doc;
    JsonArray arr = doc["profiles"].to<JsonArray>();
    
    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        if (!_summaryCache[i].inUse) continue;
        
        JsonObject item = arr.add<JsonObject>();
        item["id"] = _summaryCache[i].id;
        item["name"] = _summaryCache[i].name;
        item["brand"] = _summaryCache[i].brand;
        item["model"] = _summaryCache[i].model;
        item["year"] = _summaryCache[i].year;
        item["commandCount"] = _summaryCache[i].commandCount;
    }
    
    File file = SPIFFS.open(INDEX_FILE_PATH, "w");
    if (!file) {
        Serial.println("⚠️ [CVS] خطا در باز کردن فایل ایندکس برای نوشتن");
        return false;
    }
    
    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

// ======================== تبدیل پروفایل به JSON ========================

void CustomVehicleStore::_profileToJson(const CustomVehicleProfile& profile, JsonDocument& doc) {
    doc["id"] = profile.id;
    doc["name"] = profile.name;
    doc["brand"] = profile.brand;
    doc["model"] = profile.model;
    doc["year"] = profile.year;
    
    JsonArray cmds = doc["commands"].to<JsonArray>();
    for (int i = 0; i < profile.commandCount; i++) {
        const LearnedCommand& c = profile.commands[i];
        JsonObject item = cmds.add<JsonObject>();
        item["label"] = c.label;
        item["displayName"] = c.displayName;
        item["canId"] = c.canId;
        item["extended"] = c.isExtended;
        item["length"] = c.length;
        
        JsonArray dataArr = item["data"].to<JsonArray>();
        for (int b = 0; b < c.length && b < 8; b++) {
            dataArr.add(c.data[b]);
        }
        
        item["source"] = (int)c.source;
        item["status"] = (c.status == CMD_VERIFIED) ? "verified" : "unverified";
        item["timesObserved"] = c.timesObserved;
        item["failCount"] = c.failCount;
        item["createdAt"] = c.createdAt;
    }
}

// ======================== تبدیل JSON به پروفایل ========================

bool CustomVehicleStore::_jsonToProfile(JsonDocument& doc, CustomVehicleProfile& profile) {
    profile.id = doc["id"] | 0;
    strncpy(profile.name, doc["name"] | "", sizeof(profile.name) - 1);
    strncpy(profile.brand, doc["brand"] | "", sizeof(profile.brand) - 1);
    strncpy(profile.model, doc["model"] | "", sizeof(profile.model) - 1);
    profile.year = doc["year"] | 0;
    profile.inUse = true;
    
    profile.commandCount = 0;
    JsonArray cmds = doc["commands"].as<JsonArray>();
    for (JsonObject item : cmds) {
        if (profile.commandCount >= MAX_LEARNED_COMMANDS_PER_VEHICLE) {
            Serial.println("⚠️ [CVS] تعداد فرمان‌ها از سقف مجاز بیشتر بود - بقیه نادیده گرفته شدند");
            break;
        }
        
        LearnedCommand& c = profile.commands[profile.commandCount];
        strncpy(c.label, item["label"] | "", sizeof(c.label) - 1);
        strncpy(c.displayName, item["displayName"] | "", sizeof(c.displayName) - 1);
        c.canId = item["canId"] | 0;
        c.isExtended = item["extended"] | false;
        c.length = item["length"] | 0;
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
        c.failCount = item["failCount"] | 0;
        c.createdAt = item["createdAt"] | 0;
        
        profile.commandCount++;
    }
    
    return true;
}

// ======================== خواندن فایل پروفایل ========================

bool CustomVehicleStore::_readProfileFile(uint8_t index, CustomVehicleProfile& outProfile) {
    String path = _profilePath(index);
    if (!SPIFFS.exists(path)) {
        Serial.printf("⚠️ [CVS] فایل پروفایل یافت نشد: %s\n", path.c_str());
        return false;
    }
    
    File file = SPIFFS.open(path, "r");
    if (!file) return false;
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    
    if (err) {
        Serial.printf("⚠️ [CVS] خطا در پارس پروفایل %d: %s\n", index, err.c_str());
        return false;
    }
    
    return _jsonToProfile(doc, outProfile);
}

// ======================== نوشتن فایل پروفایل ========================

bool CustomVehicleStore::_writeProfileFile(const CustomVehicleProfile& profile) {
    JsonDocument doc;
    _profileToJson(profile, doc);
    
    String path = _profilePath(profile.id);
    File file = SPIFFS.open(path, "w");
    if (!file) {
        Serial.printf("⚠️ [CVS] خطا در باز کردن %s برای نوشتن\n", path.c_str());
        return false;
    }
    
    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}

// ======================== تعداد پروفایل‌ها ========================

uint8_t CustomVehicleStore::getProfileCount() {
    uint8_t count = 0;
    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        if (_summaryCache[i].inUse) count++;
    }
    return count;
}

// ======================== خلاصه پروفایل ========================

bool CustomVehicleStore::getProfileSummary(uint8_t index, CustomVehicleProfile& outSummary) {
    if (index >= MAX_CUSTOM_VEHICLES || !_summaryCache[index].inUse) return false;
    outSummary = _summaryCache[index];
    return true;
}

// ======================== بارگذاری کامل پروفایل ========================

bool CustomVehicleStore::loadProfile(uint8_t index, CustomVehicleProfile& outProfile) {
    if (index >= MAX_CUSTOM_VEHICLES || !_summaryCache[index].inUse) return false;
    return _readProfileFile(index, outProfile);
}

// ======================== ذخیره پروفایل ========================

bool CustomVehicleStore::saveProfile(const CustomVehicleProfile& profile) {
    if (profile.id >= MAX_CUSTOM_VEHICLES) return false;
    
    if (!_writeProfileFile(profile)) return false;
    
    // به‌روزرسانی کش خلاصه + ایندکس
    _summaryCache[profile.id] = profile;
    _summaryCache[profile.id].inUse = true;
    
    return _saveIndex();
}

// ======================== ساخت پروفایل جدید ========================

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
        Serial.printf("⚠️ [CVS] ظرفیت پروفایل‌های سفارشی پر است (حداکثر %d)\n", 
                     (int)MAX_CUSTOM_VEHICLES);
        return false;
    }
    
    CustomVehicleProfile profile;
    profile.id = (uint8_t)freeSlot;
    profile.inUse = true;
    strncpy(profile.name, name ? name : "خودروی جدید", sizeof(profile.name) - 1);
    strncpy(profile.brand, brand ? brand : "", sizeof(profile.brand) - 1);
    strncpy(profile.model, model ? model : "", sizeof(profile.model) - 1);
    profile.year = year;
    profile.commandCount = 0;
    
    if (!saveProfile(profile)) return false;
    
    outIndex = (uint8_t)freeSlot;
    Serial.printf("[CVS] پروفایل جدید ساخته شد: %s (اسلات %d)\n", profile.name, freeSlot);
    return true;
}

// ======================== حذف پروفایل ========================

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

// ======================== افزودن/به‌روزرسانی فرمان ========================

bool CustomVehicleStore::upsertCommand(uint8_t profileIndex, const LearnedCommand& cmd) {
    if (profileIndex >= MAX_CUSTOM_VEHICLES || !_summaryCache[profileIndex].inUse) {
        Serial.println("⚠️ [CVS] پروفایل مقصد وجود ندارد");
        return false;
    }
    
    CustomVehicleProfile profile;
    if (!loadProfile(profileIndex, profile)) return false;
    
    // بررسی اینکه آیا فرمانی با همین label از قبل هست (یادگیری مجدد)
    int existingIdx = -1;
    for (int i = 0; i < profile.commandCount; i++) {
        if (strcmp(profile.commands[i].label, cmd.label) == 0) {
            existingIdx = i;
            break;
        }
    }
    
    if (existingIdx >= 0) {
        profile.commands[existingIdx] = cmd;
        Serial.printf("[CVS] فرمان '%s' بازنویسی شد\n", cmd.label);
    } else {
        if (profile.commandCount >= MAX_LEARNED_COMMANDS_PER_VEHICLE) {
            Serial.println("⚠️ [CVS] ظرفیت فرمان‌های این پروفایل پر است");
            return false;
        }
        profile.commands[profile.commandCount] = cmd;
        profile.commandCount++;
        Serial.printf("[CVS] فرمان جدید '%s' اضافه شد\n", cmd.label);
    }
    
    return saveProfile(profile);
}

// ======================== تغییر وضعیت فرمان ========================

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
    
    Serial.printf("⚠️ [CVS] فرمان '%s' برای تغییر وضعیت یافت نشد\n", label);
    return false;
}

// ======================== پیدا کردن فرمان ========================

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

// ======================== صدور JSON ========================

bool CustomVehicleStore::exportProfileJSON(uint8_t index, String& outJson) {
    CustomVehicleProfile profile;
    if (!loadProfile(index, profile)) return false;
    
    JsonDocument doc;
    _profileToJson(profile, doc);
    
    outJson = "";
    serializeJson(doc, outJson);
    return outJson.length() > 0;
}

// ======================== ورود JSON ========================

bool CustomVehicleStore::importProfileJSON(const String& json, uint8_t& outIndex) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("⚠️ [CVS] JSON وارد‌شده نامعتبر است: %s\n", err.c_str());
        return false;
    }
    
    // اعتبارسنجی ساختاری حداقلی
    if (!doc["name"].is<const char*>() && !doc["name"].is<String>()) {
        Serial.println("⚠️ [CVS] فیلد 'name' در JSON ورودی یافت نشد");
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
        Serial.println("⚠️ [CVS] ظرفیت پروفایل‌ها پر است - وارد کردن ممکن نیست");
        return false;
    }
    
    CustomVehicleProfile profile;
    if (!_jsonToProfile(doc, profile)) return false;
    
    profile.id = (uint8_t)freeSlot;
    profile.inUse = true;
    
    // === سیاست امنیتی حیاتی (بخش ۶.۳ سند) ===
    // صرف‌نظر از وضعیت verified/unverified در فایل ورودی، تمام
    // فرمان‌های import‌شده به UNVERIFIED تنزل داده می‌شوند. چون این
    // دستگاه با این فرمان‌های خاص روی این خودروی خاص تست نشده -
    // حتی اگر روی دستگاه/خودروی دیگری verified بوده باشد.
    for (int i = 0; i < profile.commandCount; i++) {
        profile.commands[i].status = CMD_UNVERIFIED;
        profile.commands[i].failCount = 0;
    }
    
    if (!saveProfile(profile)) return false;
    
    outIndex = (uint8_t)freeSlot;
    Serial.printf("[CVS] پروفایل '%s' وارد شد (اسلات %d) - همه فرمان‌ها UNVERIFIED علامت خوردند\n",
                  profile.name, freeSlot);
    return true;
}
