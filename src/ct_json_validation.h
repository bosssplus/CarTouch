#ifndef CT_JSON_VALIDATION_H
#define CT_JSON_VALIDATION_H

#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>

struct CtJsonCommandFields {
    const char* label = nullptr;
    const char* displayName = nullptr;
    uint32_t canId = 0;
    bool extended = false;
    uint8_t length = 0;
    uint8_t data[8] = {0};
    int source = 2;
};

static inline bool ctValidateImportedCommand(JsonObjectConst item, CtJsonCommandFields& out) {
    if (!item["label"].is<const char*>()) return false;
    if (!item["displayName"].is<const char*>()) return false;
    if (!item["canId"].is<int>() || !item["extended"].is<bool>() || !item["length"].is<int>()) return false;
    if (!item["source"].is<int>()) return false;
    if (!item["status"].is<const char*>()) return false;
    if (!item["data"].is<JsonArrayConst>()) return false;

    const char* label = item["label"].as<const char*>();
    const char* display = item["displayName"].as<const char*>();
    int canId = item["canId"].as<int>();
    int length = item["length"].as<int>();
    int source = item["source"].as<int>();
    const char* status = item["status"].as<const char*>();

    if (!label || !display || !status || strlen(label) == 0 || strlen(label) >= 32) return false;
    if (strlen(display) >= 48) return false;
    if (canId < 0 || (uint32_t)canId > (item["extended"].as<bool>() ? 0x1FFFFFFFu : 0x7FFu)) return false;
    if (length < 0 || length > 8) return false;
    if (source < 0 || source > 2) return false;
    if (strcmp(status, "verified") != 0 && strcmp(status, "unverified") != 0) return false;

    JsonArrayConst data = item["data"].as<JsonArrayConst>();
    if (data.size() != (size_t)length || data.size() > 8) return false;
    size_t i = 0;
    for (JsonVariantConst v : data) {
        if (!v.is<int>()) return false;
        int b = v.as<int>();
        if (b < 0 || b > 255) return false;
        out.data[i++] = (uint8_t)b;
    }

    out.label = label;
    out.displayName = display;
    out.canId = (uint32_t)canId;
    out.extended = item["extended"].as<bool>();
    out.length = (uint8_t)length;
    out.source = source;
    return true;
}

#endif
