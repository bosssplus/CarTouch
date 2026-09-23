/**
 * can_manager.h - CAN Bus communication layer
 *
 * Thin wrapper around the ESP-IDF TWAI (Two-Wire Automotive Interface)
 * driver, built into the ESP32 core - no external library required.
 */

#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Types
// ============================================================================

struct CanMessage {
    uint32_t id;          // Identifier (11-bit standard or 29-bit extended)
    uint8_t  data[8];      // Payload (up to 8 bytes)
    uint8_t  length;        // Actual payload length (0-8)
    bool     isExtended;     // true  -> 29-bit identifier (CAN 2.0B)
    bool     isRemote;        // true  -> RTR frame (Remote Transmission Request)
};

enum CanError : uint8_t {
    CAN_OK             = 0,
    CAN_ERROR_INIT     = 1,
    CAN_ERROR_TX       = 2,
    CAN_ERROR_RX       = 3,
    CAN_ERROR_BUS_OFF  = 4,
    CAN_ERROR_TIMEOUT  = 5
};

// ============================================================================
// CANManager
// ============================================================================

class CANManager {
public:
    CANManager(uint8_t  txPin = PIN_CAN_TX,
               uint8_t  rxPin = PIN_CAN_RX,
               uint32_t speed = CAN_SPEED);

    // -- Lifecycle -----------------------------------------------------------
    bool begin();
    void end();

    // -- I/O -------------------------------------------------------------------
    bool sendMessage(const CanMessage& msg, uint32_t timeout = CAN_LISTEN_TIMEOUT);
    bool receiveMessage(CanMessage& msg, uint32_t timeout = CAN_LISTEN_TIMEOUT);
    bool receiveMessageNonBlocking(CanMessage& msg);
    void flushRxQueue();

    // -- Status ----------------------------------------------------------------
    bool      isActive();
    CanError  getLastError();
    bool      recoverFromBusOff();
    void      getStats(uint32_t& txCount, uint32_t& rxCount, uint32_t& errorCount);

    /**
     * Switch the live TWAI driver between TWAI_MODE_NORMAL and
     * TWAI_MODE_LISTEN_ONLY at runtime, without a full device reboot.
     *
     * Performs a full driver uninstall/reinstall under the hood - this is
     * the only way the TWAI driver supports a mode change, so the bus is
     * briefly offline (a few milliseconds) during the switch. Call this
     * only in response to an explicit user action (e.g. a settings
     * toggle), not on a hot path or timer.
     *
     * @param listenOnly true = listen-only, false = normal (TX+RX)
     * @return true if the mode switch completed successfully
     */
    bool reconfigureMode(bool listenOnly);

    /** Returns the driver's actual current mode (not just the config flag). */
    bool isListenOnlyActive();

private:
    uint8_t  _txPin;
    uint8_t  _rxPin;
    uint32_t _speed;
    bool     _initialized;
    CanError _lastError;
    uint32_t _txCount;
    uint32_t _rxCount;
    uint32_t _errorCount;

    bool _currentListenOnly;               // Mode the driver is actually running in
    bool _installAndStart(bool listenOnly); // Shared by begin() and reconfigureMode()
};

#endif // CAN_MANAGER_H
