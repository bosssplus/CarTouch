/**
 * vehicle_db.cpp - Vehicle database implementation
 *
 * DBC files (from OpenDBC) are stored in SPIFFS under /dbc and loaded
 * on demand when a vehicle is selected.
 */

#include "vehicle_db.h"
#include <FS.h>
#include <SPIFFS.h>
#include <cstring>

// ============================================================================
// Constructor
// ============================================================================

VehicleDB::VehicleDB() {
    _messageCount = 0;
    _vehicleCount = 0;
    _initialized   = false;
    memset(&_activeVehicle, 0, sizeof(VehicleProfile));
}

// ============================================================================
// Vehicle list
// ============================================================================

// Small helper to add one vehicle entry without repeating field names
static inline void _addVeh(VehicleProfile* list, uint8_t& count,
                            const char* brand, const char* model,
                            const char* file, uint16_t y0, uint16_t y1) {
    strncpy(list[count].brand,       brand, sizeof(list[count].brand) - 1);
    strncpy(list[count].model,       model, sizeof(list[count].model) - 1);
    strncpy(list[count].dbcFileName, file,  sizeof(list[count].dbcFileName) - 1);
    list[count].yearStart = y0;
    list[count].yearEnd   = y1;
    count++;
}

void VehicleDB::begin() {
    // Of the 57 DBC files bundled in data/dbc, 38 are wired here as
    // selectable vehicles. The remaining 19 are deliberately left out -
    // see the "Omitted" note at the end of this function for why.
    uint8_t i = 0;

    _addVeh(_vehicleList, i, "Generic", "OBD-II", "", 2008, 2025);  // No DBC - standard PIDs only

    // -- Toyota ---------------------------------------------------------------
    _addVeh(_vehicleList, i, "Toyota", "Prius (2010 PT)",     "/dbc/toyota_prius_2010_pt.dbc", 2010, 2015);
    _addVeh(_vehicleList, i, "Toyota", "Reference PT (2017)", "/dbc/toyota_2017_ref_pt.dbc",   2017, 2021);
    _addVeh(_vehicleList, i, "Toyota", "iQ (2009)",           "/dbc/toyota_iQ_2009_can.dbc",   2009, 2015);

    // -- BMW ------------------------------------------------------------------
    _addVeh(_vehicleList, i, "BMW", "E9x/E8x 3 Series", "/dbc/bmw_e9x_e8x.dbc", 2005, 2013);

    // -- Honda/Acura ------------------------------------------------------------
    _addVeh(_vehicleList, i, "Acura", "ILX 2016 (Nidec)", "/dbc/acura_ilx_2016_nidec.dbc", 2016, 2018);

    // -- Cadillac / GM ------------------------------------------------------------
    _addVeh(_vehicleList, i, "Cadillac", "CT6 Powertrain",        "/dbc/cadillac_ct6_powertrain.dbc", 2016, 2020);
    _addVeh(_vehicleList, i, "Cadillac", "CT6 Chassis",            "/dbc/cadillac_ct6_chassis.dbc",     2016, 2020);
    _addVeh(_vehicleList, i, "GM",       "Global A - Low Speed",   "/dbc/gm_global_a_lowspeed.dbc",      2015, 2023);
    _addVeh(_vehicleList, i, "GM",       "Global A - Chassis",     "/dbc/gm_global_a_chassis.dbc",        2015, 2023);

    // -- Chrysler / FCA --------------------------------------------------------
    _addVeh(_vehicleList, i, "Chrysler", "CUSW",                    "/dbc/chrysler_cusw.dbc",                                        2011, 2017);
    _addVeh(_vehicleList, i, "Chrysler", "Pacifica 2017 Hybrid",     "/dbc/chrysler_pacifica_2017_hybrid_private_fusion.dbc",         2017, 2020);
    _addVeh(_vehicleList, i, "FCA",      "Giorgio Platform",          "/dbc/fca_giorgio.dbc",                                          2016, 2022);

    // -- Ford ---------------------------------------------------------------------
    _addVeh(_vehicleList, i, "Ford", "Fusion 2018 PT",              "/dbc/ford_fusion_2018_pt.dbc",         2017, 2020);
    _addVeh(_vehicleList, i, "Ford", "CGEA1.2 Body (2011+)",        "/dbc/ford_cgea1_2_bodycan_2011.dbc",   2011, 2019);
    _addVeh(_vehicleList, i, "Ford", "CGEA1.2 Powertrain (2011+)",  "/dbc/ford_cgea1_2_ptcan_2011.dbc",     2011, 2019);
    // ford_lincoln_base_pt.dbc (~800 KB) and FORD_CADS*.dbc are
    // intentionally omitted - very large, multi-platform files that
    // would likely be truncated at MAX_DBC_MESSAGES=150 (a warning is
    // logged if that happens). Raise MAX_DBC_MESSAGES first if you need
    // this platform.

    // -- Hyundai --------------------------------------------------------------------
    _addVeh(_vehicleList, i, "Hyundai", "2015 C-CAN",     "/dbc/hyundai_2015_ccan.dbc",   2015, 2019);
    _addVeh(_vehicleList, i, "Hyundai", "2015 M-CAN",     "/dbc/hyundai_2015_mcan.dbc",   2015, 2019);
    _addVeh(_vehicleList, i, "Hyundai", "i30 (2014)",      "/dbc/hyundai_i30_2014.dbc",     2014, 2017);
    _addVeh(_vehicleList, i, "Hyundai", "Santa Fe (2007)", "/dbc/hyundai_santafe_2007.dbc", 2007, 2012);

    // -- Mazda ------------------------------------------------------------------------
    _addVeh(_vehicleList, i, "Mazda", "2017 Platform", "/dbc/mazda_2017.dbc",   2017, 2021);
    _addVeh(_vehicleList, i, "Mazda", "3 (2019)",        "/dbc/mazda_3_2019.dbc", 2019, 2023);
    _addVeh(_vehicleList, i, "Mazda", "RX-8",             "/dbc/mazda_rx8.dbc",     2003, 2012);

    // -- Mercedes-Benz -----------------------------------------------------------------
    _addVeh(_vehicleList, i, "Mercedes-Benz", "E350 (2010)", "/dbc/mercedes_benz_e350_2010.dbc", 2010, 2016);

    // -- MG ---------------------------------------------------------------------------------
    _addVeh(_vehicleList, i, "MG", "Generic Platform", "/dbc/mg.dbc", 2018, 2024);

    // -- Nissan -----------------------------------------------------------------------------
    _addVeh(_vehicleList, i, "Nissan", "Xterra (2011)", "/dbc/nissan_xterra_2011.dbc", 2011, 2015);

    // -- Opel -------------------------------------------------------------------------------
    _addVeh(_vehicleList, i, "Opel", "Omega (2001)", "/dbc/opel_omega_2001.dbc", 2001, 2003);

    // -- PSA (Peugeot/Citroen) --------------------------------------------------------------
    _addVeh(_vehicleList, i, "PSA", "AEE2010 R3", "/dbc/psa_aee2010_r3.dbc", 2010, 2018);

    // -- Volvo ------------------------------------------------------------------------------
    _addVeh(_vehicleList, i, "Volvo", "V40 (2017 PT)", "/dbc/volvo_v40_2017_pt.dbc", 2017, 2019);
    _addVeh(_vehicleList, i, "Volvo", "V60 (2015 PT)", "/dbc/volvo_v60_2015_pt.dbc", 2015, 2018);

    // -- Volkswagen Group ---------------------------------------------------------------------
    _addVeh(_vehicleList, i, "Volkswagen", "MQB Platform", "/dbc/vw_mqb.dbc", 2012, 2020);
    _addVeh(_vehicleList, i, "Volkswagen", "PQ Platform",  "/dbc/vw_pq.dbc",  2005, 2014);
    // vw_mlb.dbc and vw_mqbevo.dbc are omitted for the same size reason
    // as the Ford files above (~230 KB and ~113 KB respectively).

    // -- Tesla ------------------------------------------------------------------------------
    _addVeh(_vehicleList, i, "Tesla", "Generic (CAN)",      "/dbc/tesla_can.dbc",            2012, 2018);
    _addVeh(_vehicleList, i, "Tesla", "Model 3 - Vehicle",  "/dbc/tesla_model3_vehicle.dbc", 2017, 2023);

    // -- Rivian -----------------------------------------------------------------------------
    _addVeh(_vehicleList, i, "Rivian", "Primary Actuator", "/dbc/rivian_primary_actuator.dbc", 2021, 2024);

    // -- Other --------------------------------------------------------------------------------
    _addVeh(_vehicleList, i, "GWM",     "Haval H6 PHEV 2024", "/dbc/gwm_haval_h6_phev_2024.dbc", 2024, 2026);
    _addVeh(_vehicleList, i, "Hongqi",  "HS5",                 "/dbc/hongqi_hs5.dbc",              2019, 2023);
    _addVeh(_vehicleList, i, "Luxgen",  "S5 (2015)",           "/dbc/luxgen_s5_2015.dbc",           2015, 2018);

    // -- Omitted (deliberately, with reasons) --------------------------------------------------
    //
    // ESR.dbc, mazda_radar.dbc, toyota_radar_dsu_tssp.dbc, toyota_adas.dbc,
    // toyota_tss2_adas.dbc, ford_fusion_2018_adas.dbc, cadillac_ct6_object.dbc,
    // gm_global_a_object.dbc, rivian_park_assist_can.dbc
    //   -> Radar/ADAS/object-detection files with no body-control
    //      (lock/window/light) messages - not useful as a standalone
    //      vehicle selection for this project's purpose.
    //
    // gm_global_a_high_voltage_management.dbc, gm_global_a_lowspeed_1818125.dbc,
    // gm_global_a_powertrain_expansion.dbc
    //   -> GM supplementary files meant to be merged alongside
    //      gm_global_a_lowspeed.dbc. The current parser loads exactly one
    //      file per vehicle selection (see loadDBCFile) - multi-file
    //      merging would need separate work.
    //
    // FORD_CADS.dbc, FORD_CADS_64.dbc, ford_lincoln_base_pt.dbc, vw_mlb.dbc,
    // vw_mqbevo.dbc
    //   -> Too large / too many messages for the current
    //      MAX_DBC_MESSAGES=150 cap without truncation (see the Ford/VW
    //      notes above).
    //
    // comma_body.dbc, tesla_model3_party.dbc, tesla_powertrain.dbc
    //   -> Non-vehicle hardware (comma body is a robot, not a car), or
    //      redundant with tesla_model3_vehicle.dbc.
    //
    // Net result: 38 of 57 files are wired directly into the menu; the
    // remaining 19 either need multi-file merging (future work) or
    // don't represent a standalone vehicle at all.

    _vehicleCount = i;
    _initialized  = true;

    AppConfig* cfg = getConfig();
    setActiveVehicle(cfg->vehicleBrand, cfg->vehicleModel);

    Serial.printf("[DB] VehicleDB ready - %d models\n", _vehicleCount);
}

// ============================================================================
// DBC file loading
// ============================================================================

bool VehicleDB::loadDBCFile(const char* filename) {
    // Profiles like "Generic / OBD-II" intentionally have an empty
    // dbcFileName - they don't need a DBC since OBD2Reader works with
    // standard PIDs directly.
    if (!filename || filename[0] == '\0') {
        _messageCount = 0;
        Serial.println("[DB] This vehicle needs no DBC file (generic OBD-II mode)");
        return true;
    }

    if (!SPIFFS.exists(filename)) {
        Serial.printf("[DB] DBC file not found: %s\n", filename);
        return false;
    }

    File file = SPIFFS.open(filename, "r");
    if (!file) {
        Serial.printf("[DB] Failed to open file: %s\n", filename);
        return false;
    }

    _messageCount = 0;
    memset(_messages, 0, sizeof(_messages));

    Serial.printf("[DB] Loading DBC: %s\n", filename);

    char line[128];
    while (file.available() && _messageCount < MAX_DBC_MESSAGES) {
        int len = file.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';

        // If a DBC line is longer than the buffer (127 chars),
        // readBytesUntil silently truncates it, and the remainder gets
        // misparsed as a new line. Warn and discard the remainder of
        // this physical line so it isn't parsed as garbage.
        if (len == (int)(sizeof(line) - 1) && file.available()) {
            Serial.println("[DB] A DBC line exceeded the 127-char buffer and may have been truncated");
            while (file.available() && file.peek() != '\n') {
                file.read();
            }
            if (file.available()) file.read();  // consume the '\n' itself
        }

        char* cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        if (strncmp(line, "BO_ ", 4) == 0) {
            _parseMessageLine(line);
        } else if (strncmp(line, " SG_ ", 5) == 0) {
            if (_messageCount > 0) {
                _parseSignalLine(line);
            }
        } else if (strncmp(line, "CM_ ", 4) == 0) {
            _parseCommentLine(line);
        } else if (strncmp(line, "VAL_ ", 5) == 0) {
            _parseValueLine(line);
        }
    }

    if (_messageCount >= MAX_DBC_MESSAGES && file.available()) {
        Serial.printf("[DB] DBC file exceeds the %d-message cap - remaining messages were skipped\n",
                      MAX_DBC_MESSAGES);
    }

    file.close();
    Serial.printf("[DB] Load complete - %d messages\n", _messageCount);

    return _messageCount > 0;
}

// ============================================================================
// Line parsers
// ============================================================================

bool VehicleDB::_parseMessageLine(const char* line) {
    if (_messageCount >= MAX_DBC_MESSAGES) return false;

    DbcMessage* msg = &_messages[_messageCount];

    // DBC format: BO_ CAN_ID MESSAGE_NAME: DLC TRANSMITTER
    // Real example (from a file bundled in data/dbc):
    //   "BO_ 34 Active_Fault_Latched_2: 8 MRR"
    // Note there is no space before the ':'.
    uint32_t canId;
    char     name[32]        = {0};
    char     transmitter[24]  = {0};
    int      dlc;

    // "%31[^: ]" reads up to 31 chars that are neither ':' nor space -
    // unlike %s, this scanset stops at ':' too, so it correctly handles
    // both the standard no-space format ("Name: 8 XXX") and a
    // non-standard spaced one ("Name : 8 XXX"). CAN ID is read as
    // uint32_t/%u since some real DBC files use extended 29-bit IDs
    // above INT_MAX (e.g. VECTOR__INDEPENDENT_SIG_MSG).
    int parsed = sscanf(line, "BO_ %u %31[^: ] : %d %23s", &canId, name, &dlc, transmitter);

    if (parsed >= 3) {
        msg->canId = canId;
        strncpy(msg->name, name, sizeof(msg->name) - 1);
        msg->dlc = (uint8_t)dlc;
        if (parsed >= 4) {
            strncpy(msg->transmitter, transmitter, sizeof(msg->transmitter) - 1);
        }
        msg->signalCount = 0;
        _messageCount++;
        return true;
    }

    return false;
}

bool VehicleDB::_parseSignalLine(const char* line) {
    if (_messageCount == 0) {
        Serial.println("[DB] SG_ line seen before any BO_ - ignored");
        return false;
    }

    DbcMessage* msg = &_messages[_messageCount - 1];
    if (!msg || msg->signalCount >= MAX_DBC_SIGNALS) return false;

    DbcSignal* sig = &msg->signals[msg->signalCount];

    // Format: SG_ NAME : START|LENGTH@ENDIAN SIGN (SCALE,OFFSET) [MIN|MAX] UNIT TRANSMITTER
    // Example: SG_ DoorLockFL : 0|1@1+ (1,0) [0|1] "" BodyControl

    char  signalName[32] = {0};
    int   startBit, length;
    char  endian[2]       = {0};
    char  sign[2]           = {0};
    float scale, offset;
    float minVal, maxVal;
    char  unit[8]             = {0};

    int parsed = sscanf(line, " SG_ %31s : %d|%d@%1s%1s (%f,%f) [%f|%f] %7s",
                        signalName, &startBit, &length, endian, sign,
                        &scale, &offset, &minVal, &maxVal, unit);

    if (parsed >= 7) {
        strncpy(sig->name, signalName, sizeof(sig->name) - 1);
        sig->startBit = (uint8_t)startBit;
        sig->length   = (uint8_t)length;
        sig->scale    = scale;
        sig->offset   = offset;

        if (parsed >= 9) {
            sig->min = minVal;
            sig->max = maxVal;
        }
        if (parsed >= 10) {
            strncpy(sig->unit, unit, sizeof(sig->unit) - 1);
        }

        sig->type       = (sign[0] == '-') ? SIG_SIGNED : SIG_UNSIGNED;
        sig->isBigEndian = (endian[0] == '0');  // '@0' = Motorola, '@1' = Intel

        msg->signalCount++;
        return true;
    }

    return false;
}

bool VehicleDB::_parseCommentLine(const char* line) {
    // Format: CM_ BO_ CAN_ID "comment text";
    // Not currently needed for this project's use case.
    return true;
}

bool VehicleDB::_parseValueLine(const char* line) {
    // Format: VAL_ CAN_ID SIGNAL_NAME value1 "text1" value2 "text2" ...;
    // Not currently needed for this project's use case.
    return true;
}

// ============================================================================
// Lookup
// ============================================================================

DbcMessage* VehicleDB::findMessageByID(uint32_t canId) {
    for (int i = 0; i < _messageCount; i++) {
        if (_messages[i].canId == canId) {
            return &_messages[i];
        }
    }
    return nullptr;
}

DbcSignal* VehicleDB::findSignal(DbcMessage* msg, const char* signalName) {
    if (!msg) return nullptr;

    for (int i = 0; i < msg->signalCount; i++) {
        if (strcmp(msg->signals[i].name, signalName) == 0) {
            return &msg->signals[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Bit mapping (Motorola/Intel)
// ============================================================================
//
// A DBC signal's startBit is expressed in "DBC bit numbering", where
// each byte's bits are numbered MSB=7 to LSB=0. For an Intel (@1)
// signal, startBit is simply the signal's LSB and subsequent bits
// increase linearly. For a Motorola (@0) signal, startBit is the
// signal's MSB, and subsequent (less significant) bits step backward
// through the byte, wrapping to bit 7 of the next byte when they run
// past bit 0.
//
// Returns the absolute bit position (0-63) of the signal's i-th bit,
// where i=0 is the MSB for Motorola or the LSB for Intel.

static inline uint16_t _dbcBitPosition(const DbcSignal& signal, uint8_t i) {
    if (!signal.isBigEndian) {
        return signal.startBit + i;  // Intel: linear from the LSB
    }

    uint8_t byteIdx = signal.startBit / 8;
    int8_t  bitIdx  = (int8_t)(signal.startBit % 8);
    for (uint8_t step = 0; step < i; step++) {
        bitIdx--;
        if (bitIdx < 0) {
            bitIdx = 7;
            byteIdx++;
        }
    }
    return (uint16_t)byteIdx * 8 + (uint16_t)bitIdx;
}

// ============================================================================
// Signal value extraction / encoding
// ============================================================================

float VehicleDB::extractSignalValue(const DbcSignal& signal, const uint8_t* data) {
    if (signal.length == 0) return 0.0f;

    uint64_t rawValue  = 0;
    uint8_t  totalBits = signal.length;

    // i=0 maps to bit 0 of rawValue for Intel (already the LSB), and to
    // the top bit (totalBits-1) for Motorola (where i=0 is the MSB) -
    // this produces the correct numeric value in both cases.
    for (int i = 0; i < totalBits; i++) {
        uint16_t bitPos  = _dbcBitPosition(signal, i);
        uint8_t  byteIdx  = bitPos / 8;
        uint8_t  bitIdx    = bitPos % 8;

        if (byteIdx >= 8) continue;  // Outside the 8-byte frame

        bool    bitSet         = data[byteIdx] & (1 << bitIdx);
        uint8_t destBitInValue = signal.isBigEndian ? (totalBits - 1 - i) : i;

        if (bitSet) {
            rawValue |= (1ULL << destBitInValue);
        }
    }

    if (signal.type == SIG_SIGNED) {
        if (rawValue & (1ULL << (totalBits - 1))) {
            rawValue |= (~0ULL << totalBits);  // Sign-extend
        }
        return (float)((int64_t)rawValue) * signal.scale + signal.offset;
    }

    return (float)rawValue * signal.scale + signal.offset;
}

void VehicleDB::encodeSignalValue(const DbcSignal& signal, float value, uint8_t* data) {
    if (signal.length == 0) return;

    uint64_t rawValue;
    if (signal.type == SIG_SIGNED) {
        rawValue = (uint64_t)((int64_t)((value - signal.offset) / signal.scale));
    } else {
        rawValue = (uint64_t)((value - signal.offset) / signal.scale);
    }

    uint8_t totalBits = signal.length;

    // Mirrors extractSignalValue's bit mapping, in reverse.
    for (int i = 0; i < totalBits; i++) {
        uint16_t bitPos = _dbcBitPosition(signal, i);
        uint8_t  byteIdx = bitPos / 8;
        uint8_t  bitIdx   = bitPos % 8;

        if (byteIdx >= 8) continue;

        uint8_t srcBitInValue = signal.isBigEndian ? (totalBits - 1 - i) : i;
        bool    bitSet         = rawValue & (1ULL << srcBitInValue);

        if (bitSet) {
            data[byteIdx] |= (1 << bitIdx);
        } else {
            data[byteIdx] &= ~(1 << bitIdx);
        }
    }
}

// ============================================================================
// Accessors
// ============================================================================

uint8_t VehicleDB::getMessageCount() {
    return _messageCount;
}

DbcMessage* VehicleDB::getMessageByIndex(uint8_t index) {
    if (index < _messageCount) {
        return &_messages[index];
    }
    return nullptr;
}

void VehicleDB::setActiveVehicle(const char* brand, const char* model) {
    bool found = false;

    for (int i = 0; i < _vehicleCount; i++) {
        if (strcmp(_vehicleList[i].brand, brand) == 0 &&
            strcmp(_vehicleList[i].model, model) == 0) {
            _activeVehicle = _vehicleList[i];
            found = true;
            Serial.printf("[DB] Active vehicle: %s %s\n", brand, model);
            break;
        }
    }

    if (!found) {
        _activeVehicle = _vehicleList[0];
        Serial.printf("[DB] Vehicle not found, falling back to default: %s %s\n",
                      _vehicleList[0].brand, _vehicleList[0].model);
    }

    loadDBCFile(_activeVehicle.dbcFileName);
}

void VehicleDB::getActiveVehicle(char* brand, char* model, size_t maxLen) {
    strncpy(brand, _activeVehicle.brand, maxLen);
    strncpy(model, _activeVehicle.model, maxLen);
}

bool VehicleDB::getVehicleProfile(uint8_t index, VehicleProfile& profile) {
    if (index < _vehicleCount) {
        profile = _vehicleList[index];
        return true;
    }
    return false;
}

uint8_t VehicleDB::getVehicleCount() {
    return _vehicleCount;
}
