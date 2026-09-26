/**
 * error_log.cpp - In-memory error/event log + persistent counters implementation
 */

#include "error_log.h"
#include <nvs.h>
#include <stdarg.h>

static const char* NVS_NAMESPACE = "CarTouchLog";
static const char* NVS_KEY       = "counters";

// ============================================================================
// Singleton accessor (same pattern as getConfig() in config.cpp)
// ============================================================================

static ErrorLog _instance;

ErrorLog* getErrorLog() {
    return &_instance;
}

// ============================================================================
// Constructor
// ============================================================================

ErrorLog::ErrorLog() {
    _count         = 0;
    _writeIndex    = 0;
    _countersDirty = false;
    _lastSaveTime  = 0;
}

// ============================================================================
// begin()
// ============================================================================

void ErrorLog::begin() {
    _loadCountersFromNVS();
    _counters.bootCount++;
    _countersDirty = true;
    _lastSaveTime  = millis();

    // Save the incremented boot count right away rather than waiting
    // for the interval - it's one write at boot, not a hot path.
    _saveCountersToNVS();

    log(LOG_CAT_SYSTEM, LOG_INFO, "ErrorLog started (boot #%lu)", (unsigned long)_counters.bootCount);
}

// ============================================================================
// NVS load/save
// ============================================================================

void ErrorLog::_loadCountersFromNVS() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        // Namespace doesn't exist yet (first boot ever) - start at zero.
        _counters = ErrorCounters();
        return;
    }

    ErrorCounters loaded;
    size_t size = sizeof(ErrorCounters);
    err = nvs_get_blob(handle, NVS_KEY, &loaded, &size);
    nvs_close(handle);

    if (err == ESP_OK && size == sizeof(ErrorCounters) && loaded.magic == ErrorCounters().magic) {
        _counters = loaded;
    } else {
        _counters = ErrorCounters();
    }
}

void ErrorLog::_saveCountersToNVS() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        Serial.println("[ERRLOG] Failed to open NVS for saving counters");
        return;
    }

    err = nvs_set_blob(handle, NVS_KEY, &_counters, sizeof(ErrorCounters));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        _countersDirty = false;
        _lastSaveTime = millis();
    } else {
        Serial.println("[ERRLOG] Failed to save counters to NVS");
    }
}

void ErrorLog::maybeSaveCounters() {
    if (!_countersDirty) return;
    if (millis() - _lastSaveTime < ERRORLOG_NVS_SAVE_INTERVAL_MS) return;
    _saveCountersToNVS();
}

void ErrorLog::forceSaveCounters() {
    if (_countersDirty) {
        _saveCountersToNVS();
    }
}

// ============================================================================
// log()
// ============================================================================

void ErrorLog::log(LogCategory category, LogSeverity severity, const char* fmt, ...) {
    LogEntry& entry = _entries[_writeIndex];
    entry.timestamp = millis();
    entry.category  = category;
    entry.severity  = severity;

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.message, sizeof(entry.message), fmt, args);
    va_end(args);

    _writeIndex = (_writeIndex + 1) % ERRORLOG_MAX_ENTRIES;
    if (_count < ERRORLOG_MAX_ENTRIES) _count++;

    // Mirror to Serial - same visibility developers already had via
    // the individual Serial.print* calls this call wraps.
    static const char* sevStr[] = {"INFO", "WARN", "ERROR"};
    static const char* catStr[] = {"SYSTEM", "CAN", "OBD2", "WIFI", "WEB", "LEARN"};
    Serial.printf("[ERRLOG][%s][%s] %s\n", sevStr[severity], catStr[category], entry.message);

    if (severity == LOG_INFO) return;  // Only WARN/ERROR bump persistent counters

    switch (category) {
        case LOG_CAT_CAN:
            // Distinguish TX vs RX vs bus-off by message prefix convention
            // used consistently at all call sites (see can_manager.cpp).
            if (strstr(entry.message, "bus-off") || strstr(entry.message, "Bus-Off")) {
                _counters.canBusOffEvents++;
            } else if (strstr(entry.message, "receive") || strstr(entry.message, "RX") || strstr(entry.message, "Rx")) {
                _counters.canRxErrors++;
            } else {
                _counters.canTxErrors++;
            }
            break;
        case LOG_CAT_OBD2:
            _counters.obd2Timeouts++;
            break;
        case LOG_CAT_WIFI:
            _counters.wifiConnectFailures++;
            break;
        case LOG_CAT_WEB:
            _counters.webAuthFailures++;
            break;
        case LOG_CAT_LEARN:
            _counters.learnErrors++;
            break;
        default:
            break;
    }
    _countersDirty = true;
}

// ============================================================================
// Accessors
// ============================================================================

uint8_t ErrorLog::getEntryCount() {
    return _count;
}

bool ErrorLog::getEntry(uint8_t index, LogEntry& out) {
    if (index >= _count) return false;
    // index 0 = most recent = the slot just before _writeIndex
    int rawIndex = (int)_writeIndex - 1 - (int)index;
    while (rawIndex < 0) rawIndex += ERRORLOG_MAX_ENTRIES;
    out = _entries[rawIndex];
    return true;
}

const ErrorCounters& ErrorLog::getCounters() {
    return _counters;
}

// ============================================================================
// JSON serialization
// ============================================================================

String ErrorLog::toJSON(uint8_t maxEntries) {
    static const char* sevStr[] = {"info", "warn", "error"};
    static const char* catStr[] = {"system", "can", "obd2", "wifi", "web", "learn"};

    String json = "{\"type\":\"logs\",\"counters\":{";
    json += "\"bootCount\":" + String(_counters.bootCount);
    json += ",\"canTxErrors\":" + String(_counters.canTxErrors);
    json += ",\"canRxErrors\":" + String(_counters.canRxErrors);
    json += ",\"canBusOffEvents\":" + String(_counters.canBusOffEvents);
    json += ",\"obd2Timeouts\":" + String(_counters.obd2Timeouts);
    json += ",\"wifiConnectFailures\":" + String(_counters.wifiConnectFailures);
    json += ",\"webAuthFailures\":" + String(_counters.webAuthFailures);
    json += ",\"learnErrors\":" + String(_counters.learnErrors);
    json += "},\"entries\":[";

    uint8_t n = _count < maxEntries ? _count : maxEntries;
    for (uint8_t i = 0; i < n; i++) {
        LogEntry e;
        if (!getEntry(i, e)) break;

        // Escape the free-text message minimally (quotes/backslashes) -
        // it comes from fixed format strings written by this codebase,
        // not from untrusted external input, but this is cheap insurance.
        String msg;
        for (const char* p = e.message; *p; p++) {
            if (*p == '"' || *p == '\\') msg += '\\';
            msg += *p;
        }

        if (i > 0) json += ",";
        json += "{\"t\":" + String(e.timestamp);
        json += ",\"cat\":\"" + String(catStr[e.category]) + "\"";
        json += ",\"sev\":\"" + String(sevStr[e.severity]) + "\"";
        json += ",\"msg\":\"" + msg + "\"}";
    }

    json += "]}";
    return json;
}
