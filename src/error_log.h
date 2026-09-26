/**
 * error_log.h - In-memory error/event log + persistent error counters
 *
 * Part of CarTouch v2.3 (checklist item 16 - "Error logging /
 * telemetry", see PROGRESS_CHECKLIST.md and CarTouch_SPEC.md section
 * 13). Purpose: when a user reports "it stopped working" or "it
 * rebooted", give a next session (human or AI) actual evidence to
 * look at instead of only Serial output that's already gone, without
 * writing to flash often enough to wear it out.
 *
 * Two halves, intentionally different persistence models:
 *
 *   1. Ring buffer of recent events (RAM only, lost on reboot). Fine
 *      detail (timestamp + category + severity + free-text message)
 *      for whatever happened during THIS boot. Exposed read-only via
 *      GET /api/logs and WebSocket "get_logs" (both behind the same
 *      auth as every other API route).
 *
 *   2. Cumulative per-category counters (persisted to NVS). Coarse
 *      but survives reboots - "how many CAN bus-off recoveries has
 *      this device done, ever" is still answerable after a crash
 *      wiped the ring buffer. Saved at most once every
 *      ERRORLOG_NVS_SAVE_INTERVAL_MS (see main.cpp's loop()), not on
 *      every single increment, specifically so a noisy/flaky CAN bus
 *      or WiFi link cannot turn this into a flash-wear problem.
 *
 * This module never touches CANManager::sendMessage() or any
 * actuator path - it is a passive observer, called from other
 * modules' existing error-handling branches. It does not change what
 * any of those branches decide to do; it only additionally records
 * that they happened.
 */

#ifndef ERROR_LOG_H
#define ERROR_LOG_H

#include <Arduino.h>

// ============================================================================
// Categories / severity
// ============================================================================

enum LogCategory : uint8_t {
    LOG_CAT_SYSTEM = 0,   // Boot, watchdog, heap
    LOG_CAT_CAN    = 1,   // CAN/TWAI driver
    LOG_CAT_OBD2   = 2,   // OBD-II PID reads
    LOG_CAT_WIFI   = 3,   // WiFi connect/disconnect
    LOG_CAT_WEB    = 4,   // Web server, auth
    LOG_CAT_LEARN  = 5    // Learn Mode
};

enum LogSeverity : uint8_t {
    LOG_INFO  = 0,
    LOG_WARN  = 1,
    LOG_ERROR = 2
};

// ============================================================================
// A single ring-buffer entry
// ============================================================================

struct LogEntry {
    uint32_t    timestamp = 0;   // millis() at the time of logging
    LogCategory category  = LOG_CAT_SYSTEM;
    LogSeverity severity  = LOG_INFO;
    char        message[72] = {0};
};

// ============================================================================
// Persistent counters (NVS-backed, coarse, survive reboot)
// ============================================================================

struct ErrorCounters {
    uint32_t magic              = 0xE4106C01;  // Validity marker for the NVS blob
    uint32_t bootCount          = 0;
    uint32_t canTxErrors        = 0;
    uint32_t canRxErrors        = 0;
    uint32_t canBusOffEvents    = 0;
    uint32_t obd2Timeouts       = 0;
    uint32_t wifiConnectFailures    = 0;
    uint32_t webAuthFailures    = 0;
    uint32_t learnErrors        = 0;
};

#define ERRORLOG_MAX_ENTRIES         40
#define ERRORLOG_NVS_SAVE_INTERVAL_MS (5UL * 60UL * 1000UL)  // 5 minutes

class ErrorLog {
public:
    ErrorLog();

    /** Loads persistent counters from NVS (or starts them at zero if none exist yet) and increments bootCount. Call once from setup(). */
    void begin();

    /**
     * Records one event into the RAM ring buffer (oldest entry is
     * overwritten once full) and, for WARN/ERROR severities, bumps
     * the matching persistent counter in RAM (not yet written to
     * flash - see maybeSaveCounters()). Also mirrors the message to
     * Serial, same as the Serial.print* calls this replaces/wraps.
     */
    void log(LogCategory category, LogSeverity severity, const char* fmt, ...);

    /**
     * Call from the main loop(). Writes counters to NVS only if
     * ERRORLOG_NVS_SAVE_INTERVAL_MS has elapsed since the last save
     * AND at least one counter changed - keeps flash writes rare.
     */
    void maybeSaveCounters();

    /** Forces an immediate save regardless of the interval - used sparingly (e.g. right before an intentional reboot such as after OTA). */
    void forceSaveCounters();

    uint8_t   getEntryCount();
    /** index 0 = most recent. Returns false if index is out of range. */
    bool      getEntry(uint8_t index, LogEntry& out);
    const ErrorCounters& getCounters();

    /** Serializes the ring buffer + counters as a JSON string, for /api/logs and the "get_logs" WebSocket message. */
    String toJSON(uint8_t maxEntries = ERRORLOG_MAX_ENTRIES);

private:
    LogEntry       _entries[ERRORLOG_MAX_ENTRIES];
    uint8_t        _count;      // Number of valid entries (<= ERRORLOG_MAX_ENTRIES)
    uint8_t        _writeIndex; // Next slot to write (wraps)

    ErrorCounters  _counters;
    bool           _countersDirty;
    uint32_t       _lastSaveTime;

    void _saveCountersToNVS();
    void _loadCountersFromNVS();
};

/** Global accessor, same pattern as getConfig() in config.h/.cpp. */
ErrorLog* getErrorLog();

#endif // ERROR_LOG_H
