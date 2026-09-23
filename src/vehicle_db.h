/**
 * vehicle_db.h - Vehicle database (DBC parser)
 *
 * Loads and interprets DBC (CAN Database) files from the OpenDBC
 * project (comma.ai), which covers 300+ vehicle models. A DBC file
 * defines, per message: the CAN ID, byte/bit layout of each signal, and
 * scaling factors.
 *
 * This is a lightweight parser aimed at the message/signal subset
 * OpenDBC files actually use in practice; complex multiplexed DBC
 * constructs may not be fully supported.
 */

#ifndef VEHICLE_DB_H
#define VEHICLE_DB_H

#include <Arduino.h>
#include <vector>
#include "config.h"

// -- Sizing ---------------------------------------------------------------------
//
// Each DbcMessage holds its DbcSignal array inline (not as a pointer),
// so MAX_DBC_SIGNALS directly sets each message's memory footprint, and
// MAX_DBC_MESSAGES sets the total size of VehicleDB::_messages.
//
// Most real DBC messages (even complex diagnostic ones) rarely exceed
// 20-24 signals, so MAX_DBC_SIGNALS is kept at 24. With
// MAX_DBC_MESSAGES=150, VehicleDB is roughly 460 KB - too large for the
// ESP32-S3's ~512 KB internal SRAM, so it must be heap-allocated (see
// main.cpp) so the Arduino-ESP32 core serves it from PSRAM automatically.
// Raising MAX_DBC_MESSAGES further is possible but increases PSRAM use
// and boot-time parse duration; a handful of very large bundled DBC
// files (e.g. FORD_CADS_64.dbc, vw_mqb.dbc) still exceed this cap and
// will be truncated with a logged warning.
#define MAX_DBC_MESSAGES    150
#define MAX_DBC_SIGNALS     24

enum SignalType : uint8_t {
    SIG_UNSIGNED = 0,
    SIG_SIGNED   = 1,
    SIG_FLOAT    = 2,
    SIG_UNKNOWN  = 3
};

struct DbcSignal {
    char       name[32]    = {0};    // e.g. "DoorLockStatus"
    uint8_t    startBit     = 0;      // Start bit, as defined by the raw DBC (see isBigEndian)
    uint8_t    length        = 0;      // Signal length, bits
    SignalType type          = SIG_UNSIGNED;
    float      scale         = 1.0f;
    float      offset         = 0.0f;
    float      min             = 0.0f;
    float      max             = 100.0f;
    char       unit[8]        = {0};    // e.g. "km/h"
    char       comment[64]      = {0};
    bool       isMultiplexed     = false;
    uint8_t    multiplexValue     = 0;

    // DBC signal byte order: '@0' = Motorola/big-endian (bit numbering
    // from each byte's MSB), '@1' = Intel/little-endian (from the LSB).
    // Both are supported - see extractSignalValue()/encodeSignalValue()
    // in vehicle_db.cpp for the bit-mapping logic.
    bool isBigEndian = false;   // true = Motorola (@0), false = Intel (@1)
};

struct DbcMessage {
    uint32_t   canId         = 0;
    char       name[32]       = {0};   // e.g. "DoorStatus"
    uint8_t    dlc              = 8;
    char       transmitter[24]   = {0};
    uint8_t    signalCount        = 0;
    DbcSignal  signals[MAX_DBC_SIGNALS];
};

struct VehicleProfile {
    char     brand[24]        = {0};   // e.g. "Toyota"
    char     model[24]         = {0};   // e.g. "Camry"
    uint16_t yearStart          = 0;
    uint16_t yearEnd             = 0;
    char     dbcFileName[32]     = {0};
};

class VehicleDB {
public:
    VehicleDB();

    /** Initializes the vehicle list. */
    void begin();

    /** Loads and parses a DBC file from SPIFFS (e.g. "/dbc/toyota.dbc"). */
    bool loadDBCFile(const char* filename);

    /** Finds a loaded message by CAN ID. Returns nullptr if not found. */
    DbcMessage* findMessageByID(uint32_t canId);

    /** Finds a signal within a message by name. Returns nullptr if not found. */
    DbcSignal* findSignal(DbcMessage* msg, const char* signalName);

    /** Extracts a signal's scaled value from a raw 8-byte CAN payload. */
    float extractSignalValue(const DbcSignal& signal, const uint8_t* data);

    /** Encodes a signal's value into a raw 8-byte CAN payload. */
    void encodeSignalValue(const DbcSignal& signal, float value, uint8_t* data);

    uint8_t getMessageCount();
    DbcMessage* getMessageByIndex(uint8_t index);

    void setActiveVehicle(const char* brand, const char* model);
    void getActiveVehicle(char* brand, char* model, size_t maxLen);

    /** Retrieves a supported-vehicle list entry by index. */
    bool getVehicleProfile(uint8_t index, VehicleProfile& profile);
    uint8_t getVehicleCount();

private:
    DbcMessage      _messages[MAX_DBC_MESSAGES];
    uint8_t          _messageCount;
    VehicleProfile   _activeVehicle;

    // 40 entries - covers the 38 vehicles wired in begin(), with a small
    // safety margin.
    VehicleProfile   _vehicleList[40];
    uint8_t           _vehicleCount;
    bool               _initialized;

    bool _parseMessageLine(const char* line);
    bool _parseSignalLine(const char* line);
    bool _parseValueLine(const char* line);
    bool _parseCommentLine(const char* line);
};

#endif // VEHICLE_DB_H
