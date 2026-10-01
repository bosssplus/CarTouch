#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>

/**
 * BLE control/status and firmware OTA manager.
 *
 * The BLE OTA protocol is intentionally simple so it can be used by a
 * generic GATT client (for example nRF Connect or a dedicated mobile app):
 *   1) write "START:<password>:<firmware_size>" to the command characteristic
 *   2) write raw firmware bytes to the data characteristic
 *   3) write "END" to the command characteristic
 *   4) device validates the Update object and reboots after success
 *
 * The same Web password is required before a BLE OTA session can start.
 */
class BLEManager {
public:
    BLEManager();

    bool begin();
    void update();
    bool isEnabled() const;
    bool isConnected() const;
    bool isOtaInProgress() const;
    uint32_t otaBytesReceived() const;
    uint32_t otaExpectedBytes() const;
    const char* deviceName() const;

private:
    bool _started;
    bool _connected;
    bool _otaInProgress;
    bool _otaAuthenticated;
    bool _otaError;
    uint8_t  _otaFailCount = 0;
    uint32_t _otaLockUntil = 0;
    uint32_t _otaExpected;
    uint32_t _otaReceived;
    uint32_t _rebootAt;
    String _deviceName;

    void _sendStatus(const char* status);
    void _handleCommand(const String& command);
    bool _startOta(uint32_t size, const String& password);
    void _abortOta();
    bool _finishOta();

    class ServerCallbacks;
    class CommandCallbacks;
    class DataCallbacks;

    friend class ServerCallbacks;
    friend class CommandCallbacks;
    friend class DataCallbacks;
};

extern BLEManager bleManager;

#endif
