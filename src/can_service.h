#ifndef CAN_SERVICE_H
#define CAN_SERVICE_H

#include "can_interface.h"

enum CanBusId : uint8_t {
    CAN_BUS_0 = 0,
    CAN_BUS_1 = 1
};

class CANService : public CanInterface {
public:
    CANService(CanInterface& can0, CanInterface& can1);

    bool begin() override;
    bool begin(CanBusId bus);
    void end() override;
    void end(CanBusId bus);
    bool sendMessage(const CanMessage& msg,
                     uint32_t timeout = CAN_INTERFACE_TIMEOUT_MS) override;
    bool sendMessage(CanBusId bus, const CanMessage& msg,
                     uint32_t timeout = CAN_INTERFACE_TIMEOUT_MS);
    bool receiveMessage(CanMessage& msg,
                        uint32_t timeout = CAN_INTERFACE_TIMEOUT_MS) override;
    bool receiveMessage(CanBusId bus, CanMessage& msg,
                        uint32_t timeout = CAN_INTERFACE_TIMEOUT_MS);
    bool receiveMessageNonBlocking(CanMessage& msg) override;
    bool receiveMessageNonBlocking(CanBusId bus, CanMessage& msg);
    void flushRxQueue() override;
    void flushRxQueue(CanBusId bus);
    bool isActive() override;
    bool isActive(CanBusId bus);
    CanError getLastError() override;
    CanError getLastError(CanBusId bus);
    bool recoverFromBusOff() override;
    bool recoverFromBusOff(CanBusId bus);
    void getStats(uint32_t& txCount, uint32_t& rxCount, uint32_t& errorCount) override;
    void getStats(CanBusId bus, uint32_t& txCount, uint32_t& rxCount,
                  uint32_t& errorCount);
    bool getDiagnostics(CanDiagnostics& out) override;
    bool getDiagnostics(CanBusId bus, CanDiagnostics& out);
    uint32_t getLastRxTime() const override;
    uint32_t getLastRxTime(CanBusId bus) const;
    bool reconfigureMode(bool listenOnly) override;
    bool reconfigureMode(CanBusId bus, bool listenOnly);
    bool isListenOnlyActive() override;
    bool isListenOnlyActive(CanBusId bus);

private:
    CanInterface& _can0;
    CanInterface& _can1;
    CanInterface* _get(CanBusId bus);
    const CanInterface* _get(CanBusId bus) const;
};

#endif