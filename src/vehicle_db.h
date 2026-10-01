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
// DBC messages are kept in a bounded table, while each message owns a dynamic
// vector of signals. This avoids silently dropping signals from real-world DBCs
// that contain more than the old fixed 24-signal limit. The message table is
// large enough for the bundled direct-menu profiles (including BMW E9x/E8x).
#define MAX_DBC_MESSAGES 400

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ DBC data types
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

enum SignalType : uint8_t {
    SIG_UNSIGNED = 0,
    SIG_SIGNED   = 1,
    SIG_FLOAT    = 2,
    SIG_UNKNOWN  = 3
};

struct DbcSignal {
    char       name[32]    = {0};         // e.g. "DoorLockStatus"
    uint8_t    startBit     = 0;          // Start bit, as defined by the raw DBC (see isBigEndian)
    uint8_t    length        = 0;         // Signal length, bits
    SignalType type   = SIG_UNSIGNED;
    float      scale  = 1.0f;
    float      offset = 0.0f;
    float      min    = 0.0f;
    float      max    = 100.0f;
    char       unit[8]        = {0};      // e.g. "km/h"
    char       comment[64]      = {0};
    bool    isMultiplexed  = false;
    uint8_t multiplexValue = 0;

    // DBC signal byte order: '@0' = Motorola/big-endian (bit numbering
    // from each byte's MSB), '@1' = Intel/little-endian (from the LSB).
    // Both are supported - see extractSignalValue()/encodeSignalValue()
    // in vehicle_db.cpp for the bit-mapping logic.
    bool isBigEndian = false;    // true = Motorola (@0), false = Intel (@1)
};

struct DbcMessage {
    uint32_t   canId         = 0;
    bool       isExtended    = false;  // true = 29-bit CAN frame
    char       name[32]       = {0};        // e.g. "DoorStatus"
    uint8_t    dlc              = 8;
    char       transmitter[24]   = {0};
    uint16_t   signalCount        = 0;
    std::vector<DbcSignal> signals;
};

struct VehicleProfile {
    char     brand[24]        = {0};       // e.g. "Toyota"
    char     model[24]         = {0};      // e.g. "Camry"
    uint16_t yearStart = 0;
    uint16_t yearEnd   = 0;
    char     dbcFileName[32]     = {0};
};

class VehicleDB {

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Public API
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

public:
    VehicleDB();

    /** Initializes the vehicle list. */
    void begin();

    /** Loads and parses a DBC file from SPIFFS (e.g. "/dbc/toyota.dbc"). */
    bool loadDBCFile(const char* filename);

    /** Finds a loaded message by CAN ID and frame format. Returns nullptr if not found. */
    DbcMessage* findMessageByID(uint32_t canId, bool isExtended);

    /** Backward-compatible lookup; prefer the overload with isExtended. */
    DbcMessage* findMessageByID(uint32_t canId);

    /** Finds a signal within a message by name. Returns nullptr if not found. */
    DbcSignal* findSignal(DbcMessage* msg, const char* signalName);

    /** Extracts a signal's scaled value from a raw 8-byte CAN payload. */
    float extractSignalValue(const DbcSignal& signal, const uint8_t* data);

    /** Encodes a signal's value into a raw 8-byte CAN payload. */
    void encodeSignalValue(const DbcSignal& signal, float value, uint8_t* data);

    uint16_t getMessageCount();
    DbcMessage* getMessageByIndex(uint16_t index);

    /**
     * Selects the active vehicle and loads its DBC.
     * @param allowFallback if true (boot only) an unknown brand/model falls
     *        back to the first list entry. If false, an unknown vehicle
     *        leaves the current selection untouched - a control device must
     *        never silently send another vehicle's frames.
     * @return true if the vehicle was found and its DBC loaded.
     */
    bool setActiveVehicle(const char* brand, const char* model, bool allowFallback = false);
    void getActiveVehicle(char* brand, char* model, size_t maxLen);

    /** Retrieves a supported-vehicle list entry by index. */
    bool getVehicleProfile(uint8_t index, VehicleProfile& profile);
    uint8_t getVehicleCount();

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Parser internals
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

private:
    DbcMessage      _messages[MAX_DBC_MESSAGES];
    uint16_t         _messageCount;
    VehicleProfile   _activeVehicle;

    // 40 entries - covers the selectable profiles wired in begin(), including
    // the Generic OBD-II entry, with a small safety margin.
    VehicleProfile   _vehicleList[40];
    uint8_t           _vehicleCount;
    bool               _initialized;

    bool _parseMessageLine(const char* line);
    bool _parseSignalLine(const char* line);
    bool _parseValueLine(const char* line);
    bool _parseCommentLine(const char* line);
    void _clearMessages();
};

#endif    // VEHICLE_DB_H
