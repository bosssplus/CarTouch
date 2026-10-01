#ifndef CAN_INTERFACE_H
#define CAN_INTERFACE_H

#include <stdint.h>

static const uint32_t CAN_INTERFACE_TIMEOUT_MS = 50;

struct CanMessage {
    uint32_t id;
    uint8_t data[8];
    uint8_t length;
    bool isExtended;
    bool isRemote;
};

struct CanDiagnostics {
    bool driverReady;
    bool listenOnly;
    bool busActive;
    bool busOff;
    uint32_t rxFrames;
    uint32_t txFrames;
    uint32_t errorCount;
    uint32_t msgsWaiting;
    uint32_t txErrorCounter;
    uint32_t rxErrorCounter;
    uint32_t txFailedCount;
    uint32_t rxMissedCount;
    uint32_t rxOverrunCount;
    uint32_t arbitrationLostCount;
    uint32_t busErrorCount;
};

enum CanError : uint8_t {
    CAN_OK             = 0,
    CAN_ERROR_INIT     = 1,
    CAN_ERROR_TX       = 2,
    CAN_ERROR_RX       = 3,
    CAN_ERROR_BUS_OFF  = 4,
    CAN_ERROR_TIMEOUT  = 5
};

class CanInterface {
public:
    virtual ~CanInterface() {}
    virtual bool begin() = 0;
    virtual void end() = 0;
    virtual bool sendMessage(const CanMessage& msg,
                             uint32_t timeout = CAN_INTERFACE_TIMEOUT_MS) = 0;
    virtual bool receiveMessage(CanMessage& msg,
                                uint32_t timeout = CAN_INTERFACE_TIMEOUT_MS) = 0;
    virtual bool receiveMessageNonBlocking(CanMessage& msg) = 0;
    virtual void flushRxQueue() = 0;
    virtual bool isActive() = 0;
    virtual CanError getLastError() = 0;
    virtual bool recoverFromBusOff() = 0;
    virtual void getStats(uint32_t& txCount, uint32_t& rxCount, uint32_t& errorCount) = 0;
    virtual bool getDiagnostics(CanDiagnostics& out) = 0;
    virtual uint32_t getLastRxTime() const = 0;
    virtual bool reconfigureMode(bool listenOnly) = 0;
    virtual bool isListenOnlyActive() = 0;
};

#endif