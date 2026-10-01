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
#include "can_interface.h"

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Types
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ CANManager
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

class CANManager : public CanInterface {

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Public API
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

public:
    CANManager(uint8_t  txPin = PIN_CAN_TX,
               uint8_t  rxPin = PIN_CAN_RX,
               uint32_t speed = CAN_SPEED);

    // -- Lifecycle -----------------------------------------------------------
    bool begin() override;
    void end() override;

    bool setPins(uint8_t txPin, uint8_t rxPin);
    uint8_t getTxPin() const { return _txPin; }
    uint8_t getRxPin() const { return _rxPin; }

    // -- I/O -------------------------------------------------------------------
    bool sendMessage(const CanMessage& msg, uint32_t timeout = CAN_LISTEN_TIMEOUT) override;
    bool receiveMessage(CanMessage& msg, uint32_t timeout = CAN_LISTEN_TIMEOUT) override;
    bool receiveMessageNonBlocking(CanMessage& msg) override;
    void flushRxQueue() override;

    // -- Status ----------------------------------------------------------------
    bool      isActive() override;
    CanError  getLastError() override;
    bool      recoverFromBusOff() override;
    void      getStats(uint32_t& txCount, uint32_t& rxCount, uint32_t& errorCount) override;
    bool      getDiagnostics(CanDiagnostics& out) override;
    uint32_t  getLastRxTime() const override;

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
    bool reconfigureMode(bool listenOnly) override;

    /** Returns the driver's actual current mode (not just the config flag). */
    bool isListenOnlyActive() override;

private:
    uint8_t  _txPin;
    uint8_t  _rxPin;
    uint32_t _speed;
    volatile bool _initialized;                // read from several tasks (Web/BLE/loop)
    CanError _lastError;
    uint32_t _txCount;
    uint32_t _rxCount;
    uint32_t _errorCount;
    uint32_t _lastRxTime;

    volatile bool _currentListenOnly;                   // Mode the driver is actually running in
    bool _installAndStart(bool listenOnly);    // Shared by begin() and reconfigureMode()
    bool _stopAndUninstall();                  // Safe from RUNNING / BUS_OFF / RECOVERING states
};

#endif    // CAN_MANAGER_H
