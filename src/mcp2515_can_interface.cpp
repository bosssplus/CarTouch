#include "config.h"
#undef CAN_SPEED
#include <mcp2515.h>
#include "mcp2515_can_interface.h"
#include <SPI.h>
#include <new>
#include <string.h>
#include "ct_time.h"
#include "ct_tx_guard.h"

static bool mapMcpBitrate(uint32_t bitrate, CAN_SPEED& speed) {
    switch (bitrate) {
        case 100000: speed = CAN_100KBPS; return true;
        case 125000: speed = CAN_125KBPS; return true;
        case 250000: speed = CAN_250KBPS; return true;
        case 500000: speed = CAN_500KBPS; return true;
        case 1000000: speed = CAN_1000KBPS; return true;
        default: return false;
    }
}

Mcp2515CanInterface::Mcp2515CanInterface(uint8_t csPin, uint8_t intPin,
                                         uint32_t speed, bool listenOnly)
    : _driver(nullptr), _csPin(csPin), _intPin(intPin), _speed(speed),
      _initialized(false), _listenOnly(listenOnly), _lastError(CAN_OK),
      _txCount(0), _rxCount(0), _errorCount(0), _rxOverrunCount(0),
      _lastRxTime(0) {}

bool Mcp2515CanInterface::begin() {
    const AppConfig* config = getConfig();
    _csPin = config->can1CsPin;
    _intPin = config->can1IntPin;
    _speed = config->can1Speed;
    _listenOnly = config->can1ListenOnly;

    CAN_SPEED speed;
    if (!mapMcpBitrate(_speed, speed) || !validateCan1Pins(_csPin, _intPin)) {
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);
    pinMode(_intPin, INPUT_PULLUP);
    SPI.begin(PIN_TFT_SCLK, PIN_TFT_MISO, PIN_TFT_MOSI);

    if (!_driver) {
        _driver = new (std::nothrow) MCP2515(_csPin, MCP2515_SPI_CLOCK, &SPI);
        if (!_driver) {
            _lastError = CAN_ERROR_INIT;
            return false;
        }
    }

    if (_driver->reset() != MCP2515::ERROR_OK || !_configure(_listenOnly)) {
        _initialized = false;
        _lastError = CAN_ERROR_INIT;
        return false;
    }

    _initialized = true;
    _lastError = CAN_OK;
    return true;
}

bool Mcp2515CanInterface::_configure(bool listenOnly) {
    CAN_SPEED speed;
    if (!_driver || !mapMcpBitrate(_speed, speed)) return false;
    if (_driver->setBitrate(speed, MCP_8MHZ) != MCP2515::ERROR_OK) return false;
    const MCP2515::ERROR modeResult = listenOnly
        ? _driver->setListenOnlyMode()
        : _driver->setNormalMode();
    if (modeResult != MCP2515::ERROR_OK) return false;
    _listenOnly = listenOnly;
    return true;
}

void Mcp2515CanInterface::end() {
    if (_driver && _initialized) _driver->setSleepMode();
    _initialized = false;
}

bool Mcp2515CanInterface::sendMessage(const CanMessage& msg, uint32_t) {
    const CtTxGuardResult admission = ctTxGuard(_initialized, _listenOnly, msg.length);
    if (admission != CT_TX_OK) {
        _lastError = admission == CT_TX_ERR_NOT_INITIALIZED ? CAN_ERROR_INIT : CAN_ERROR_TX;
        return false;
    }
    if (!ctTxIdValid(msg.id, msg.isExtended)) {
        _lastError = CAN_ERROR_TX;
        return false;
    }

    can_frame frame = {};
    frame.can_id = msg.id;
    if (msg.isExtended) frame.can_id |= CAN_EFF_FLAG;
    if (msg.isRemote) frame.can_id |= CAN_RTR_FLAG;
    frame.can_dlc = msg.length;
    memcpy(frame.data, msg.data, msg.length);

    if (_driver->sendMessage(&frame) != MCP2515::ERROR_OK) {
        ++_errorCount;
        _lastError = CAN_ERROR_TX;
        return false;
    }
    ++_txCount;
    _lastError = CAN_OK;
    return true;
}

bool Mcp2515CanInterface::receiveMessage(CanMessage& msg, uint32_t timeout) {
    const uint32_t start = millis();
    do {
        if (receiveMessageNonBlocking(msg)) return true;
        delay(1);
    } while (!ctElapsedAtLeast(millis(), start, timeout));
    _lastError = CAN_ERROR_TIMEOUT;
    return false;
}

bool Mcp2515CanInterface::receiveMessageNonBlocking(CanMessage& msg) {
    if (!_initialized || !_driver) {
        _lastError = CAN_ERROR_INIT;
        return false;
    }
    if (digitalRead(_intPin) != LOW && !_driver->checkReceive()) return false;

    can_frame frame = {};
    const MCP2515::ERROR result = _driver->readMessage(&frame);
    if (result == MCP2515::ERROR_NOMSG) return false;
    if (result != MCP2515::ERROR_OK || frame.can_dlc > sizeof(msg.data)) {
        ++_errorCount;
        _lastError = CAN_ERROR_RX;
        return false;
    }

    const bool extended = (frame.can_id & CAN_EFF_FLAG) != 0;
    msg.id = frame.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    msg.isExtended = extended;
    msg.isRemote = (frame.can_id & CAN_RTR_FLAG) != 0;
    msg.length = frame.can_dlc;
    memcpy(msg.data, frame.data, msg.length);
    ++_rxCount;
    _lastRxTime = millis();
    _lastError = CAN_OK;
    return true;
}

void Mcp2515CanInterface::flushRxQueue() {
    CanMessage msg;
    while (receiveMessageNonBlocking(msg)) {}
}

bool Mcp2515CanInterface::isActive() {
    return _initialized;
}

CanError Mcp2515CanInterface::getLastError() {
    return _lastError;
}

bool Mcp2515CanInterface::recoverFromBusOff() {
    if (!_driver) return false;
    _initialized = false;
    if (_driver->reset() != MCP2515::ERROR_OK || !_configure(_listenOnly)) {
        _lastError = CAN_ERROR_BUS_OFF;
        return false;
    }
    _initialized = true;
    _lastError = CAN_OK;
    return true;
}

void Mcp2515CanInterface::getStats(uint32_t& txCount, uint32_t& rxCount,
                                   uint32_t& errorCount) {
    txCount = _txCount;
    rxCount = _rxCount;
    errorCount = _errorCount;
}

bool Mcp2515CanInterface::getDiagnostics(CanDiagnostics& out) {
    memset(&out, 0, sizeof(out));
    out.driverReady = _initialized;
    out.listenOnly = _listenOnly;
    out.rxFrames = _rxCount;
    out.txFrames = _txCount;
    out.errorCount = _errorCount;
    out.rxOverrunCount = _rxOverrunCount;
    out.rxMissedCount = _rxOverrunCount;
    if (!_initialized || !_driver) return false;

    const uint8_t flags = _driver->getErrorFlags();
    out.busOff = (flags & MCP2515::EFLG_TXBO) != 0;
    out.busActive = !out.busOff;
    out.txErrorCounter = _driver->errorCountTX();
    out.rxErrorCounter = _driver->errorCountRX();
    if (flags & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR)) {
        ++_rxOverrunCount;
        _driver->clearRXnOVRFlags();
    }
    out.rxOverrunCount = _rxOverrunCount;
    out.rxMissedCount = _rxOverrunCount;
    return true;
}

uint32_t Mcp2515CanInterface::getLastRxTime() const {
    return _lastRxTime;
}

bool Mcp2515CanInterface::reconfigureMode(bool listenOnly) {
    if (!_initialized) return false;
    if (!_configure(listenOnly)) {
        _lastError = CAN_ERROR_INIT;
        return false;
    }
    _lastError = CAN_OK;
    return true;
}

bool Mcp2515CanInterface::isListenOnlyActive() {
    return _initialized && _listenOnly;
}