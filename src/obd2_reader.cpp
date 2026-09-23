/**
 * obd2_reader.cpp - OBD-II reader implementation
 *
 * ISO 15765-4 (CAN 11-bit). Request ID 0x7DF, reply IDs 0x7E8/0x7E9.
 */

#include "obd2_reader.h"

// ============================================================================
// OBD-II constants
// ============================================================================

#define OBD_REQUEST_ID     0x7DF   // Broadcast request
#define OBD_REPLY_ID       0x7E8   // ECU #1 reply
#define OBD_REPLY_ID_2     0x7E9   // ECU #2 reply (if present)

#define OBD_MODE_CURRENT    0x01   // Show current data
#define OBD_MODE_FREEZE     0x02   // Freeze frame data
#define OBD_MODE_DTC        0x03   // Read DTCs
#define OBD_MODE_CLEAR_DTC  0x04   // Clear DTCs

#define PID_SUPPORTED_1     0x00   // Supported PIDs 0x01-0x20
#define PID_SUPPORTED_2     0x20   // Supported PIDs 0x21-0x40
#define PID_SUPPORTED_3     0x40   // Supported PIDs 0x41-0x60

// ============================================================================
// Constructor
// ============================================================================

OBD2Reader::OBD2Reader(CANManager& canManager) : _can(canManager) {
    _lastError        = 0;
    _lastRequestTime   = 0;
    _requestInterval    = 50;   // Minimum spacing between requests (ms)

    _pollState          = OBD_POLL_IDLE;
    _pollIndex          = 0;
    _pollWaitStartMs     = 0;
    _hasCompletedRound   = false;
    _pollIntervalMs       = 200;  // Spacing between completed rounds
    _lastRoundStartMs      = 0;
}

void OBD2Reader::begin() {
    _can.flushRxQueue();
    Serial.println("[OBD2] Reader ready");
}

// ============================================================================
// Single-PID request [BLOCKING]
// ============================================================================

bool OBD2Reader::requestPID(uint8_t pid, ObdResponse& response) {
    uint32_t now = millis();
    if (now - _lastRequestTime < _requestInterval) {
        delay(_requestInterval - (now - _lastRequestTime));
    }

    // Request frame: [Mode, PID, 0x00 x 5]
    CanMessage request;
    request.id          = OBD_REQUEST_ID;
    request.isExtended  = false;
    request.isRemote    = false;
    request.length      = 8;
    request.data[0]     = 0x02;              // Valid byte count
    request.data[1]     = OBD_MODE_CURRENT;
    request.data[2]     = pid;
    request.data[3]     = 0x00;
    request.data[4]     = 0x00;
    request.data[5]     = 0x00;
    request.data[6]     = 0x00;
    request.data[7]     = 0x00;

    if (!_can.sendMessage(request)) {
        _lastError = 1;
        response.success = false;
        return false;
    }

    _lastRequestTime = millis();

    CanMessage reply;
    uint32_t timeout = millis() + 200;
    bool received = false;

    while (millis() < timeout) {
        if (_can.receiveMessage(reply, 50)) {
            if (reply.id == OBD_REPLY_ID || reply.id == OBD_REPLY_ID_2) {
                if (reply.length >= 3 &&
                    reply.data[1] == (OBD_MODE_CURRENT + 0x40) &&
                    reply.data[2] == pid) {
                    received = true;
                    break;
                }
            }
        }
    }

    if (!received) {
        _lastError = 2;
        response.success = false;
        return false;
    }

    response.pid        = pid;
    response.length      = reply.length - 3;  // Strip header
    response.success      = true;
    response.timestamp     = millis();

    for (int i = 0; i < response.length && i < 6; i++) {
        response.data[i] = reply.data[i + 3];
    }

    _lastError = 0;
    return true;
}

// ============================================================================
// Single-value readers [BLOCKING]
// ============================================================================

uint16_t OBD2Reader::readEngineRPM() {
    ObdResponse response;
    if (!requestPID(OBD_PID_ENGINE_RPM, response)) return 0;
    if (response.length >= 2) {
        return ((uint16_t)response.data[0] * 256 + response.data[1]) / 4;
    }
    return 0;
}

uint8_t OBD2Reader::readVehicleSpeed() {
    ObdResponse response;
    if (!requestPID(OBD_PID_VEHICLE_SPEED, response)) return 0;
    if (response.length >= 1) {
        return response.data[0];
    }
    return 0;
}

int8_t OBD2Reader::readCoolantTemp() {
    ObdResponse response;
    if (!requestPID(OBD_PID_COOLANT_TEMP, response)) return -40;
    if (response.length >= 1) {
        return response.data[0] - 40;
    }
    return -40;
}

uint8_t OBD2Reader::readThrottlePosition() {
    ObdResponse response;
    if (!requestPID(OBD_PID_THROTTLE_POS, response)) return 0;
    if (response.length >= 1) {
        return (uint8_t)((float)response.data[0] * 100.0f / 255.0f);
    }
    return 0;
}

uint8_t OBD2Reader::readFuelLevel() {
    ObdResponse response;
    if (!requestPID(OBD_PID_FUEL_LEVEL, response)) return 0;
    if (response.length >= 1) {
        return (uint8_t)((float)response.data[0] * 100.0f / 255.0f);
    }
    return 0;
}

uint16_t OBD2Reader::readEngineRuntime() {
    ObdResponse response;
    if (!requestPID(OBD_PID_RUNTIME, response)) return 0;
    if (response.length >= 2) {
        return ((uint16_t)response.data[0] * 256 + response.data[1]);
    }
    return 0;
}

// ============================================================================
// Read all PIDs [BLOCKING - kept for backward compatibility]
// ============================================================================
// No longer called from the main loop() - use update() + getLatestData().

void OBD2Reader::readAllPIDs(VehicleData& data) {
    data.engineRPM = readEngineRPM();
    delay(10);
    data.vehicleSpeed = readVehicleSpeed();
    delay(10);
    data.coolantTemp = readCoolantTemp();
    delay(10);
    data.throttlePos = readThrottlePosition();
    delay(10);
    data.fuelLevel = readFuelLevel();
    delay(10);
    data.engineRuntime = readEngineRuntime();
}

// ============================================================================
// Non-blocking poll state machine
// ============================================================================
// Round order: RPM -> Speed -> Coolant -> Throttle -> Fuel -> Runtime -> done
// -> pause _pollIntervalMs -> next round. No delay() and no wait loop
// anywhere here; each update() call does at most one send or one
// non-blocking receive check, then returns immediately.

void OBD2Reader::_applyPidToData(uint8_t pid, const ObdResponse& resp, VehicleData& data) {
    if (!resp.success) return;  // Timed-out PID: leave the previous value untouched

    switch (pid) {
        case OBD_PID_ENGINE_RPM:
            if (resp.length >= 2)
                data.engineRPM = ((uint16_t)resp.data[0] * 256 + resp.data[1]) / 4;
            break;
        case OBD_PID_VEHICLE_SPEED:
            if (resp.length >= 1) data.vehicleSpeed = resp.data[0];
            break;
        case OBD_PID_COOLANT_TEMP:
            if (resp.length >= 1) data.coolantTemp = (int8_t)(resp.data[0] - 40);
            break;
        case OBD_PID_THROTTLE_POS:
            if (resp.length >= 1)
                data.throttlePos = (uint8_t)((float)resp.data[0] * 100.0f / 255.0f);
            break;
        case OBD_PID_FUEL_LEVEL:
            if (resp.length >= 1)
                data.fuelLevel = (uint8_t)((float)resp.data[0] * 100.0f / 255.0f);
            break;
        case OBD_PID_RUNTIME:
            if (resp.length >= 2)
                data.engineRuntime = ((uint16_t)resp.data[0] * 256 + resp.data[1]);
            break;
    }
}

// Poll order - must stay in sync with _applyPidToData
static const uint8_t OBD_POLL_PID_TABLE[6] = {
    OBD_PID_ENGINE_RPM, OBD_PID_VEHICLE_SPEED, OBD_PID_COOLANT_TEMP,
    OBD_PID_THROTTLE_POS, OBD_PID_FUEL_LEVEL, OBD_PID_RUNTIME
};

void OBD2Reader::_pollStartNextPid() {
    uint8_t pid = OBD_POLL_PID_TABLE[_pollIndex];

    CanMessage request;
    request.id          = OBD_REQUEST_ID;
    request.isExtended  = false;
    request.isRemote    = false;
    request.length      = 8;
    request.data[0]     = 0x02;
    request.data[1]     = OBD_MODE_CURRENT;
    request.data[2]     = pid;
    request.data[3]     = 0x00;
    request.data[4]     = 0x00;
    request.data[5]     = 0x00;
    request.data[6]     = 0x00;
    request.data[7]     = 0x00;

    if (_can.sendMessage(request)) {
        _pollWaitStartMs = millis();
        _pollState = OBD_POLL_WAITING;
    } else {
        // Send failed - skip this PID rather than stalling the round
        _pollIndex++;
        _pollState = (_pollIndex >= _POLL_PID_COUNT) ? OBD_POLL_DONE : OBD_POLL_SENDING;
    }
}

void OBD2Reader::_pollCheckResponse() {
    uint8_t pid = OBD_POLL_PID_TABLE[_pollIndex];

    CanMessage reply;
    bool gotSomething = _can.receiveMessageNonBlocking(reply);

    if (gotSomething) {
        if ((reply.id == OBD_REPLY_ID || reply.id == OBD_REPLY_ID_2) &&
            reply.length >= 3 &&
            reply.data[1] == (OBD_MODE_CURRENT + 0x40) &&
            reply.data[2] == pid) {

            ObdResponse resp;
            resp.pid        = pid;
            resp.length      = reply.length - 3;
            resp.success      = true;
            resp.timestamp     = millis();
            for (int i = 0; i < resp.length && i < 6; i++) {
                resp.data[i] = reply.data[i + 3];
            }
            _applyPidToData(pid, resp, _pendingData);

            _pollIndex++;
            _pollState = (_pollIndex >= _POLL_PID_COUNT) ? OBD_POLL_DONE : OBD_POLL_SENDING;
            return;
        }
        // Some other bus traffic - rechecked next update() call, no wait
        return;
    }

    // Nothing queued this call - check for timeout (no delay/no loop)
    if (millis() - _pollWaitStartMs > 200) {
        ObdResponse timeoutResp;
        timeoutResp.success = false;
        _applyPidToData(pid, timeoutResp, _pendingData);  // Previous value kept

        _pollIndex++;
        _pollState = (_pollIndex >= _POLL_PID_COUNT) ? OBD_POLL_DONE : OBD_POLL_SENDING;
    }
    // Otherwise: still waiting, checked again next update() call
}

void OBD2Reader::update() {
    uint32_t now = millis();

    switch (_pollState) {
        case OBD_POLL_IDLE:
            if (now - _lastRoundStartMs >= _pollIntervalMs) {
                _pendingData = _hasCompletedRound ? _latestData : VehicleData();
                _pollIndex = 0;
                _lastRoundStartMs = now;
                _pollState = OBD_POLL_SENDING;
            }
            break;

        case OBD_POLL_SENDING:
            if (now - _lastRequestTime >= _requestInterval) {
                _pollStartNextPid();
                _lastRequestTime = now;
            }
            break;

        case OBD_POLL_WAITING:
            _pollCheckResponse();
            break;

        case OBD_POLL_DONE:
            _latestData.engineRPM      = _pendingData.engineRPM;
            _latestData.vehicleSpeed    = _pendingData.vehicleSpeed;
            _latestData.coolantTemp     = _pendingData.coolantTemp;
            _latestData.throttlePos     = _pendingData.throttlePos;
            _latestData.fuelLevel        = _pendingData.fuelLevel;
            _latestData.engineRuntime     = _pendingData.engineRuntime;
            _hasCompletedRound = true;
            _pollState = OBD_POLL_IDLE;
            break;
    }
}

bool OBD2Reader::getLatestData(VehicleData& outData) {
    if (!_hasCompletedRound) return false;
    outData.engineRPM      = _latestData.engineRPM;
    outData.vehicleSpeed    = _latestData.vehicleSpeed;
    outData.coolantTemp     = _latestData.coolantTemp;
    outData.throttlePos     = _latestData.throttlePos;
    outData.fuelLevel        = _latestData.fuelLevel;
    outData.engineRuntime     = _latestData.engineRuntime;
    return true;
}

ObdPollState OBD2Reader::getPollState() {
    return _pollState;
}

// ============================================================================
// PID support check
// ============================================================================

bool OBD2Reader::isPidSupported(uint8_t pid) {
    ObdResponse response;
    if (!requestPID(PID_SUPPORTED_1, response)) return false;

    // 4-byte bitmask response: bits 31-0 map to PIDs 0x01-0x20
    if (response.length >= 4 && pid >= 0x01 && pid <= 0x20) {
        uint32_t supported = 0;
        for (int i = 0; i < 4; i++) {
            supported = (supported << 8) | response.data[i];
        }
        return (supported >> (32 - pid)) & 1;
    }

    return false;
}

// ============================================================================
// DTC read / clear
// ============================================================================

uint8_t OBD2Reader::readDTCs(uint16_t dtcList[], uint8_t maxCount) {
    CanMessage request;
    request.id          = OBD_REQUEST_ID;
    request.isExtended  = false;
    request.isRemote    = false;
    request.length      = 8;
    request.data[0]     = 0x01;
    request.data[1]     = OBD_MODE_DTC;
    request.data[2]     = 0x00;
    request.data[3]     = 0x00;
    request.data[4]     = 0x00;
    request.data[5]     = 0x00;
    request.data[6]     = 0x00;
    request.data[7]     = 0x00;

    _can.sendMessage(request);

    CanMessage reply;
    uint8_t dtcCount = 0;
    uint32_t timeout = millis() + 500;

    while (millis() < timeout && dtcCount < maxCount) {
        if (_can.receiveMessage(reply, 100)) {
            if (reply.id == OBD_REPLY_ID && reply.length >= 3) {
                // First byte is the DTC count; each subsequent pair is one DTC
                for (int i = 3; i + 1 < reply.length && dtcCount < maxCount; i += 2) {
                    dtcList[dtcCount] = ((uint16_t)reply.data[i] << 8) | reply.data[i + 1];
                    dtcCount++;
                }
            }
        }
    }

    return dtcCount;
}

bool OBD2Reader::clearDTCs() {
    CanMessage request;
    request.id          = OBD_REQUEST_ID;
    request.isExtended  = false;
    request.isRemote    = false;
    request.length      = 8;
    request.data[0]     = 0x01;
    request.data[1]     = OBD_MODE_CLEAR_DTC;
    request.data[2]     = 0x00;
    request.data[3]     = 0x00;
    request.data[4]     = 0x00;
    request.data[5]     = 0x00;
    request.data[6]     = 0x00;
    request.data[7]     = 0x00;

    return _can.sendMessage(request);
}

uint8_t OBD2Reader::getLastError() {
    return _lastError;
}
