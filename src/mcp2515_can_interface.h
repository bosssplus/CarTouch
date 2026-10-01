#ifndef MCP2515_CAN_INTERFACE_H
#define MCP2515_CAN_INTERFACE_H

#include "config.h"
#include "can_interface.h"

class MCP2515;

class Mcp2515CanInterface : public CanInterface {
public:
    explicit Mcp2515CanInterface(uint8_t csPin = PIN_CAN1_CS,
                                 uint8_t intPin = PIN_CAN1_INT,
                                 uint32_t speed = CAN1_SPEED,
                                 bool listenOnly = CAN1_LISTEN_ONLY);

    bool begin() override;
    void end() override;
    bool sendMessage(const CanMessage& msg,
                     uint32_t timeout = CAN_INTERFACE_TIMEOUT_MS) override;
    bool receiveMessage(CanMessage& msg,
                        uint32_t timeout = CAN_INTERFACE_TIMEOUT_MS) override;
    bool receiveMessageNonBlocking(CanMessage& msg) override;
    void flushRxQueue() override;
    bool isActive() override;
    CanError getLastError() override;
    bool recoverFromBusOff() override;
    void getStats(uint32_t& txCount, uint32_t& rxCount, uint32_t& errorCount) override;
    bool getDiagnostics(CanDiagnostics& out) override;
    uint32_t getLastRxTime() const override;
    bool reconfigureMode(bool listenOnly) override;
    bool isListenOnlyActive() override;

private:
    MCP2515* _driver;
    uint8_t _csPin;
    uint8_t _intPin;
    uint32_t _speed;
    bool _initialized;
    bool _listenOnly;
    CanError _lastError;
    uint32_t _txCount;
    uint32_t _rxCount;
    uint32_t _errorCount;
    uint32_t _rxOverrunCount;
    uint32_t _lastRxTime;

    bool _configure(bool listenOnly);
};

#endif