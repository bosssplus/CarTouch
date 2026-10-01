/**
 * can_manager.cpp - CAN Bus communication layer (TWAI implementation)
 *
 * CAN0 uses an external transceiver; verify its TXD/RXD logic levels are
 * compatible with the ESP32-S3 before wiring the module.
 */

#include "can_manager.h"
#include "driver/twai.h"
#include "error_log.h"
#include "ct_tx_guard.h"
#include "ct_time.h"

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Constructor
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

CANManager::CANManager(uint8_t txPin, uint8_t rxPin, uint32_t speed) {
    _txPin              = txPin;
    _rxPin              = rxPin;
    _speed              = speed;
    _initialized        = false;
    _lastError          = CAN_OK;
    _txCount            = 0;
    _rxCount            = 0;
    _errorCount         = 0;
    _lastRxTime          = 0;
    _currentListenOnly  = false;     // Set for real in begin() / _installAndStart()
}

bool CANManager::setPins(uint8_t txPin, uint8_t rxPin) {
    if (!validateCanPins(txPin, rxPin)) {
        Serial.printf("[CAN] Refusing invalid/conflicting pins: TX=%u RX=%u\n", txPin, rxPin);
        return false;
    }
    if (_initialized) {
        Serial.println("[CAN] Pin changes require CAN to be stopped/restarted");
        return false;
    }
    _txPin = txPin;
    _rxPin = rxPin;
    return true;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Driver install + start (shared by begin() and reconfigureMode())
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CANManager::_installAndStart(bool listenOnly) {
    twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)_txPin,
        (gpio_num_t)_rxPin,
        listenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL
    );
    gConfig.rx_queue_len = 64;    // default (5) drops frames on a busy car bus

    twai_timing_config_t tConfig;
    switch (_speed) {
        case 100000:  tConfig = TWAI_TIMING_CONFIG_100KBITS();  break;
        case 125000:  tConfig = TWAI_TIMING_CONFIG_125KBITS();  break;
        case 250000:  tConfig = TWAI_TIMING_CONFIG_250KBITS();  break;
        case 500000:  tConfig = TWAI_TIMING_CONFIG_500KBITS();  break;
        case 800000:  tConfig = TWAI_TIMING_CONFIG_800KBITS();  break;
        case 1000000: tConfig = TWAI_TIMING_CONFIG_1MBITS();    break;
        default:
            Serial.printf("[CAN] Invalid speed %lu, defaulting to 500Kbps\n", (unsigned long)_speed);
            tConfig = TWAI_TIMING_CONFIG_500KBITS();
            break;
    }

    // Accept all messages (sniff everything on the bus)
    twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&gConfig, &tConfig, &fConfig);
    if (err != ESP_OK) {
        Serial.printf("[CAN] Driver install failed: %d\n", (int)err);
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    err = twai_start();
    if (err != ESP_OK) {
        Serial.printf("[CAN] Driver start failed: %d\n", (int)err);
        twai_driver_uninstall();
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    _currentListenOnly = listenOnly;
    _lastError = CAN_OK;
    return true;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Safe stop + uninstall
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

// twai_stop() is illegal in BUS_OFF/RECOVERING (ESP-IDF returns
// ESP_ERR_INVALID_STATE) and twai_driver_uninstall() then fails because the
// driver is not STOPPED. Handling only the RUNNING case left the driver
// installed forever after a bus-off, so every later mode switch or
// reinstall failed until the next reboot.
bool CANManager::_stopAndUninstall() {
    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK) {
        // Driver not installed at all - nothing to stop.
        return true;
    }

    if (status.state == TWAI_STATE_BUS_OFF) {
        if (twai_initiate_recovery() != ESP_OK) return false;
    }
    if (status.state == TWAI_STATE_BUS_OFF || status.state == TWAI_STATE_RECOVERING) {
        const uint32_t recoveryStart = millis();
        while (!ctElapsedAtLeast(millis(), recoveryStart, 1500)) {
            if (twai_get_status_info(&status) != ESP_OK) return false;
            if (status.state == TWAI_STATE_STOPPED) break;
            delay(10);
        }
        if (status.state != TWAI_STATE_STOPPED) return false;
    } else if (status.state == TWAI_STATE_RUNNING) {
        if (twai_stop() != ESP_OK) return false;
    }

    return twai_driver_uninstall() == ESP_OK;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Lifecycle
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CANManager::begin() {
    // Load persisted CAN GPIO mapping before installing TWAI.
    AppConfig* cfg = getConfig();
    if (validateCanPins(cfg->canTxPin, cfg->canRxPin)) {
        _txPin = cfg->canTxPin;
        _rxPin = cfg->canRxPin;
    } else {
        _txPin = PIN_CAN_TX;
        _rxPin = PIN_CAN_RX;
    }

    if (isValidCanSpeed(cfg->canSpeed)) {
        _speed = cfg->canSpeed;
    } else {
        _speed = CAN_SPEED;
    }

    Serial.printf("[CAN] Init: TX=%d, RX=%d, Speed=%lu bps\n",
                  _txPin, _rxPin, (unsigned long)_speed);

    bool listenOnly = cfg->listenOnlyMode;
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
        _initialized = false;    // Block TX first, whatever happens below
        if (!_stopAndUninstall()) {
            Serial.println("[CAN] Stop/uninstall failed - restart the device to recover CAN");
            _lastError = CAN_ERROR_INIT;
            return;
        }
        Serial.println("[CAN] CAN Bus stopped");
    }
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Runtime mode switching (Listen-Only <-> Normal)
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

bool CANManager::reconfigureMode(bool listenOnly) {
    if (!_initialized) {
        return _installAndStart(listenOnly) && (_initialized = true);
    }

    if (_currentListenOnly == listenOnly) {
        return true;    // Already in the requested mode - nothing to do
    }

    Serial.printf("[CAN] Switching driver mode to %s ...\n",
                  listenOnly ? "Listen-Only" : "Normal");

    // Full uninstall/reinstall - the TWAI driver has no in-place mode change.
    // Block TX before touching the driver so no other task can transmit
    // while it is being torn down.
    _initialized = false;
    if (!_stopAndUninstall()) {
        Serial.println("[CAN] Stop/uninstall failed during mode switch - CAN left inactive");
        _lastError = CAN_ERROR_INIT;
        return false;
    }

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

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Send
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CANManager::sendMessage(const CanMessage& msg, uint32_t timeout) {
    // Central TX admission guard (pure, unit-tested in test/test_tx_guard).
    // Defense-in-depth: never attempt a transmit while the TWAI driver is
    // ACTUALLY running in Listen-Only mode, regardless of the caller.
    switch (ctTxGuard(_initialized, _currentListenOnly, msg.length)) {
        case CT_TX_OK:
            break;
        case CT_TX_ERR_NOT_INITIALIZED:
            _lastError = CAN_ERROR_INIT;
            return false;
        case CT_TX_ERR_LISTEN_ONLY:
            _lastError = CAN_ERROR_TX;
            Serial.println("[CAN] TX rejected while Listen-Only is active");
            return false;
        case CT_TX_ERR_LENGTH:
            Serial.println("[CAN] Message length exceeds 8 bytes");
            return false;
    }

    // Reject identifiers wider than the frame format: the hardware would mask
    // them and transmit a different (wrong) CAN ID.
    if (!ctTxIdValid(msg.id, msg.isExtended)) {
        _lastError = CAN_ERROR_TX;
        Serial.printf("[CAN] TX rejected: ID 0x%lX does not fit a %s frame\n",
                      (unsigned long)msg.id, msg.isExtended ? "29-bit" : "11-bit");
        return false;
    }

    twai_message_t twaiMsg = {};    // zero all flag bits (ss/self/dlc_non_comp)
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
        getErrorLog()->log(LOG_CAT_CAN, LOG_WARN, "Send timeout, ID 0x%03lX", (unsigned long)msg.id);
    } else {
        _lastError = CAN_ERROR_TX;
        getErrorLog()->log(LOG_CAT_CAN, LOG_WARN, "Send error %d, ID 0x%03lX", (int)err, (unsigned long)msg.id);

        twai_status_info_t status;
        twai_get_status_info(&status);
        if (status.state == TWAI_STATE_BUS_OFF) {
            getErrorLog()->log(LOG_CAT_CAN, LOG_ERROR, "Bus-off detected - attempting recovery");
            recoverFromBusOff();
        }
    }

    return false;
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Receive (blocking)
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

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
        _lastRxTime = millis();
        _lastError = CAN_OK;
        return true;
    }

    if (err == ESP_ERR_TIMEOUT) {
        return false;    // Normal - no message was waiting
    }

    _errorCount++;
    _lastError = CAN_ERROR_RX;
    getErrorLog()->log(LOG_CAT_CAN, LOG_WARN, "Receive error %d", err);
    return false;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Receive (non-blocking)
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

bool CANManager::receiveMessageNonBlocking(CanMessage& msg) {
    return receiveMessage(msg, 0);
}

void CANManager::flushRxQueue() {
    CanMessage dummy;
    while (receiveMessageNonBlocking(dummy)) {
        // Discard everything currently queued
    }
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Status
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

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
    if (!_initialized) return false;

    Serial.println("[CAN] Starting Bus-Off recovery...");

    twai_status_info_t status;
    if (twai_get_status_info(&status) != ESP_OK) {
        _lastError = CAN_ERROR_BUS_OFF;
        return false;
    }

    // Per ESP-IDF TWAI docs, twai_stop() must NOT be called from the BUS_OFF
    // state. The documented recovery sequence is:
    //   BUS_OFF -> twai_initiate_recovery() -> RECOVERING -> STOPPED -> twai_start()
    // twai_initiate_recovery() also flushes the TX queue.
    if (status.state == TWAI_STATE_BUS_OFF) {
        esp_err_t err = twai_initiate_recovery();
        if (err != ESP_OK) {
            getErrorLog()->log(LOG_CAT_CAN, LOG_ERROR, "Bus-off recovery: initiate failed: %d", err);
            _lastError = CAN_ERROR_BUS_OFF;
            return false;
        }
    } else if (status.state == TWAI_STATE_RUNNING) {
        // Controller already running again - nothing to do.
        _lastError = CAN_OK;
        return true;
    }

    // Wait (bounded) for the controller to reach STOPPED. From BUS_OFF this
    // requires 128 occurrences of the bus-free signal before the driver stops.
    const uint32_t deadline = millis() + 1500;
    while (millis() < deadline) {
        if (twai_get_status_info(&status) != ESP_OK) break;
        if (status.state == TWAI_STATE_STOPPED) break;
        delay(10);
    }

    if (status.state != TWAI_STATE_STOPPED) {
        getErrorLog()->log(LOG_CAT_CAN, LOG_WARN, "Bus-off recovery: not stopped yet (state=%d)", (int)status.state);
        _lastError = CAN_ERROR_BUS_OFF;
        return false;
    }

    esp_err_t err = twai_start();
    if (err == ESP_OK) {
        _lastError = CAN_OK;
        getErrorLog()->log(LOG_CAT_CAN, LOG_INFO, "Bus-off recovery successful");
        return true;
    }

    getErrorLog()->log(LOG_CAT_CAN, LOG_ERROR, "Bus-off recovery failed: %d", err);
    _lastError = CAN_ERROR_BUS_OFF;
    return false;
}

void CANManager::getStats(uint32_t& txCount, uint32_t& rxCount, uint32_t& errorCount) {
    txCount    = _txCount;
    rxCount    = _rxCount;
    errorCount = _errorCount;
}


// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Diagnostics
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

bool CANManager::getDiagnostics(CanDiagnostics& out) {
    memset(&out, 0, sizeof(out));
    out.driverReady = _initialized;
    out.listenOnly = _currentListenOnly;
    out.rxFrames = _rxCount;
    out.txFrames = _txCount;
    out.errorCount = _errorCount;

    if (!_initialized) return false;

    twai_status_info_t status;
    esp_err_t err = twai_get_status_info(&status);
    if (err != ESP_OK) return false;

    out.busOff = (status.state == TWAI_STATE_BUS_OFF);
    out.busActive = (status.state == TWAI_STATE_RUNNING ||
                     status.state == TWAI_STATE_RECOVERING);
    out.msgsWaiting = status.msgs_to_rx;
    out.txErrorCounter = status.tx_error_counter;
    out.rxErrorCounter = status.rx_error_counter;
    out.txFailedCount = status.tx_failed_count;
    out.rxMissedCount = status.rx_missed_count;
    out.rxOverrunCount = status.rx_overrun_count;
    out.arbitrationLostCount = status.arb_lost_count;
    out.busErrorCount = status.bus_error_count;
    return true;
}

uint32_t CANManager::getLastRxTime() const {
    return _lastRxTime;
}
