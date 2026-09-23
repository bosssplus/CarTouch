/**
 * can_manager.cpp - CAN Bus communication layer (TWAI implementation)
 *
 * SN65HVD230 is a 3.3V transceiver, wired directly to the ESP32-S3 with
 * no level shifting required.
 */

#include "can_manager.h"
#include "driver/twai.h"

// ============================================================================
// Constructor
// ============================================================================

CANManager::CANManager(uint8_t txPin, uint8_t rxPin, uint32_t speed) {
    _txPin              = txPin;
    _rxPin              = rxPin;
    _speed              = speed;
    _initialized        = false;
    _lastError          = CAN_OK;
    _txCount            = 0;
    _rxCount            = 0;
    _errorCount         = 0;
    _currentListenOnly  = false;  // Set for real in begin() / _installAndStart()
}

// ============================================================================
// Driver install + start (shared by begin() and reconfigureMode())
// ============================================================================

bool CANManager::_installAndStart(bool listenOnly) {
    twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)_txPin,
        (gpio_num_t)_rxPin,
        listenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL
    );

    twai_timing_config_t tConfig;
    switch (_speed) {
        case 100000:  tConfig = TWAI_TIMING_CONFIG_100KBITS();  break;
        case 125000:  tConfig = TWAI_TIMING_CONFIG_125KBITS();  break;
        case 250000:  tConfig = TWAI_TIMING_CONFIG_250KBITS();  break;
        case 500000:  tConfig = TWAI_TIMING_CONFIG_500KBITS();  break;
        case 800000:  tConfig = TWAI_TIMING_CONFIG_800KBITS();  break;
        case 1000000: tConfig = TWAI_TIMING_CONFIG_1MBITS();    break;
        default:
            Serial.printf("[CAN] Invalid speed %d, defaulting to 500Kbps\n", _speed);
            tConfig = TWAI_TIMING_CONFIG_500KBITS();
            break;
    }

    // Accept all messages (sniff everything on the bus)
    twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&gConfig, &tConfig, &fConfig);
    if (err != ESP_OK) {
        Serial.printf("[CAN] Driver install failed: %d\n", err);
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    err = twai_start();
    if (err != ESP_OK) {
        Serial.printf("[CAN] Driver start failed: %d\n", err);
        twai_driver_uninstall();
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    _currentListenOnly = listenOnly;
    _lastError = CAN_OK;
    return true;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool CANManager::begin() {
    Serial.printf("[CAN] Init: TX=%d, RX=%d, Speed=%d bps\n", _txPin, _rxPin, _speed);

    bool listenOnly = getConfig()->listenOnlyMode;
    if (listenOnly) {
        Serial.println("[CAN] Listen-Only mode enabled");
    }

    if (!_installAndStart(listenOnly)) {
        return false;
    }

    _initialized = true;
    Serial.println("[CAN] CAN Bus initialized successfully");
    return true;
}

void CANManager::end() {
    if (_initialized) {
        twai_stop();
        twai_driver_uninstall();
        _initialized = false;
        Serial.println("[CAN] CAN Bus stopped");
    }
}

// ============================================================================
// Runtime mode switching (Listen-Only <-> Normal)
// ============================================================================

bool CANManager::reconfigureMode(bool listenOnly) {
    if (!_initialized) {
        return _installAndStart(listenOnly) && (_initialized = true);
    }

    if (_currentListenOnly == listenOnly) {
        return true;  // Already in the requested mode - nothing to do
    }

    Serial.printf("[CAN] Switching driver mode to %s ...\n",
                  listenOnly ? "Listen-Only" : "Normal");

    // Full uninstall/reinstall - the TWAI driver has no in-place mode change.
    twai_stop();
    esp_err_t uninstallErr = twai_driver_uninstall();
    if (uninstallErr != ESP_OK) {
        Serial.printf("[CAN] Uninstall failed during mode switch: %d\n", uninstallErr);
        _lastError = CAN_ERROR_INIT;
        _initialized = false;  // Unknown state - treat as inactive to be safe
        return false;
    }

    _initialized = false;  // Bus is briefly offline until reinstall completes

    if (!_installAndStart(listenOnly)) {
        Serial.println("[CAN] Reinstall after mode switch failed - CAN left inactive");
        return false;
    }

    _initialized = true;
    Serial.println("[CAN] Mode switch complete");
    return true;
}

bool CANManager::isListenOnlyActive() {
    return _initialized && _currentListenOnly;
}

// ============================================================================
// Send
// ============================================================================

bool CANManager::sendMessage(const CanMessage& msg, uint32_t timeout) {
    if (!_initialized) {
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    if (msg.length > 8) {
        Serial.println("[CAN] Message length exceeds 8 bytes");
        return false;
    }

    twai_message_t twaiMsg;
    twaiMsg.identifier        = msg.id;
    twaiMsg.extd              = msg.isExtended ? 1 : 0;
    twaiMsg.rtr               = msg.isRemote ? 1 : 0;
    twaiMsg.data_length_code  = msg.length;

    for (int i = 0; i < msg.length; i++) {
        twaiMsg.data[i] = msg.data[i];
    }

    esp_err_t err = twai_transmit(&twaiMsg, pdMS_TO_TICKS(timeout));

    if (err == ESP_OK) {
        _txCount++;
        _lastError = CAN_OK;
        return true;
    }

    _errorCount++;

    if (err == ESP_ERR_TIMEOUT) {
        _lastError = CAN_ERROR_TIMEOUT;
        Serial.println("[CAN] Send timeout");
    } else {
        _lastError = CAN_ERROR_TX;
        Serial.printf("[CAN] Send error: %d\n", err);

        twai_status_info_t status;
        twai_get_status_info(&status);
        if (status.state == TWAI_STATE_BUS_OFF) {
            Serial.println("[CAN] Bus-Off detected - attempting recovery");
            recoverFromBusOff();
        }
    }

    return false;
}

// ============================================================================
// Receive (blocking)
// ============================================================================

bool CANManager::receiveMessage(CanMessage& msg, uint32_t timeout) {
    if (!_initialized) {
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    twai_message_t twaiMsg;
    esp_err_t err = twai_receive(&twaiMsg, pdMS_TO_TICKS(timeout));

    if (err == ESP_OK) {
        msg.id          = twaiMsg.identifier;
        msg.isExtended  = twaiMsg.extd;
        msg.isRemote    = twaiMsg.rtr;

        // Clamp DLC defensively: CAN Classic caps at 8 bytes, but a driver
        // bug or malformed frame could report more, which would overrun
        // msg.data[8] in the copy loop below.
        uint8_t dlc = twaiMsg.data_length_code;
        if (dlc > 8) {
            Serial.printf("[CAN] Invalid DLC received: %d - clamped to 8\n", dlc);
            dlc = 8;
        }
        msg.length = dlc;

        for (int i = 0; i < msg.length; i++) {
            msg.data[i] = twaiMsg.data[i];
        }
        _rxCount++;
        _lastError = CAN_OK;
        return true;
    }

    if (err == ESP_ERR_TIMEOUT) {
        return false;  // Normal - no message was waiting
    }

    _errorCount++;
    _lastError = CAN_ERROR_RX;
    return false;
}

// ============================================================================
// Receive (non-blocking)
// ============================================================================

bool CANManager::receiveMessageNonBlocking(CanMessage& msg) {
    return receiveMessage(msg, 0);
}

void CANManager::flushRxQueue() {
    CanMessage dummy;
    while (receiveMessageNonBlocking(dummy)) {
        // Discard everything currently queued
    }
}

// ============================================================================
// Status
// ============================================================================

bool CANManager::isActive() {
    if (!_initialized) return false;

    twai_status_info_t status;
    twai_get_status_info(&status);
    return (status.state != TWAI_STATE_STOPPED &&
            status.state != TWAI_STATE_BUS_OFF);
}

CanError CANManager::getLastError() {
    return _lastError;
}

bool CANManager::recoverFromBusOff() {
    Serial.println("[CAN] Starting Bus-Off recovery...");

    twai_stop();
    delay(100);  // Let the bus settle

    esp_err_t err = twai_start();
    if (err == ESP_OK) {
        _lastError = CAN_OK;
        Serial.println("[CAN] Bus-Off recovery successful");
        return true;
    }

    Serial.printf("[CAN] Recovery failed: %d\n", err);
    _lastError = CAN_ERROR_BUS_OFF;
    return false;
}

void CANManager::getStats(uint32_t& txCount, uint32_t& rxCount, uint32_t& errorCount) {
    txCount    = _txCount;
    rxCount    = _rxCount;
    errorCount = _errorCount;
}
