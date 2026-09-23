/**
 * obd2_reader.h - OBD-II data acquisition
 *
 * Reads standard OBD-II data (SAE J1979) from the vehicle's ECU - RPM,
 * speed, temperature, etc. Protocol: ISO 15765-4 (CAN 11-bit, 500 Kbps).
 *
 * Polling is implemented as a non-blocking state machine: update() must
 * be called every loop() iteration and advances one small step per call
 * (send a request, or check the receive queue without blocking) rather
 * than waiting for a response inline. The older single-PID functions
 * (readEngineRPM, etc.) are kept for API compatibility but are marked
 * [BLOCKING] - they wait up to ~200ms per call and must not be used on
 * the main loop path. Prefer update() / getLatestData().
 */

#ifndef OBD2_READER_H
#define OBD2_READER_H

#include <Arduino.h>
#include "config.h"
#include "can_manager.h"

// State machine states for the non-blocking multi-PID poll cycle
enum ObdPollState : uint8_t {
    OBD_POLL_IDLE = 0,   // Between rounds, waiting for the next one to start
    OBD_POLL_SENDING,     // Sending the request for the current PID
    OBD_POLL_WAITING,     // Waiting for the current PID's response (no delay/loop)
    OBD_POLL_DONE          // A full round (all PIDs) has completed
};

struct ObdResponse {
    uint8_t  pid;         // Requested PID
    uint8_t  data[6];      // Response payload (up to 6 bytes)
    uint8_t  length;        // Actual payload length
    bool     success;        // true if the response is valid
    uint32_t timestamp;      // Reception time (millis)
};

class OBD2Reader {
public:
    OBD2Reader(CANManager& canManager);

    void begin();

    /**
     * [BLOCKING] Sends a request and waits for the response to a single
     * PID - blocks for up to ~200ms. Do not call from the main loop;
     * suitable for manual debugging or calls outside the main loop. Use
     * update()/getLatestData() for periodic background polling.
     */
    bool requestPID(uint8_t pid, ObdResponse& response);

    /** [BLOCKING] See requestPID note above. */
    uint16_t readEngineRPM();
    uint8_t  readVehicleSpeed();
    int8_t   readCoolantTemp();
    uint8_t  readThrottlePosition();
    uint8_t  readFuelLevel();
    uint16_t readEngineRuntime();

    /**
     * [BLOCKING - kept for backward compatibility only] Reads all base
     * PIDs sequentially (up to ~1.2s worst case). No longer called from
     * main.cpp/loop() - use update() instead. Kept only in case other
     * code still depends on this exact signature.
     */
    void readAllPIDs(VehicleData& data);

    /**
     * [NON-BLOCKING] Must be called every loop() iteration (like
     * tftUI.update()). Advances the periodic poll state machine by one
     * step, respecting the minimum request interval: sends a request or
     * checks the receive queue without blocking. Never calls delay() or
     * waits in a loop.
     */
    void update();

    /**
     * Returns the most recently completed poll round. If no round has
     * completed yet, returns default VehicleData values (zero/UNKNOWN).
     * @return true if at least one full round has completed
     */
    bool getLatestData(VehicleData& outData);

    /** Current poll state machine status (for UI/debug status display). */
    ObdPollState getPollState();

    /** Checks whether the ECU supports a given PID (via PID 0x00). */
    bool isPidSupported(uint8_t pid);

    /** Reads stored diagnostic trouble codes (DTCs). */
    uint8_t readDTCs(uint16_t dtcList[], uint8_t maxCount = MAX_DTC_COUNT);

    /** Clears stored DTCs. */
    bool clearDTCs();

    uint8_t getLastError();

private:
    CANManager& _can;
    uint8_t     _lastError;
    uint32_t    _lastRequestTime;
    uint32_t    _requestInterval;  // Minimum spacing between requests (ms)

    bool _sendOBDRequest(uint8_t pid, uint8_t expectedDataLength);
    bool _parseOBDResponse(const uint8_t* rawData, uint8_t length,
                            uint8_t pid, ObdResponse& response);

    // -- Non-blocking poll state machine (update()) --------------------------
    static const uint8_t _POLL_PID_COUNT = 6;
    ObdPollState _pollState;
    uint8_t      _pollIndex;           // Current PID index within _POLL_PID_COUNT
    uint32_t     _pollWaitStartMs;      // When the current PID's wait started
    VehicleData  _pendingData;          // Data being assembled this round
    VehicleData  _latestData;           // Most recently completed round
    bool         _hasCompletedRound;
    uint32_t     _pollIntervalMs;       // Spacing between rounds (not between PIDs)
    uint32_t     _lastRoundStartMs;

    void _pollStartNextPid();    // Sends the current PID's request (non-blocking)
    void _pollCheckResponse();   // Non-blocking receive-queue check
    void _applyPidToData(uint8_t pid, const ObdResponse& resp, VehicleData& data);
};

#endif // OBD2_READER_H
