#include "can_service.h"

CANService::CANService(CanInterface& can0, CanInterface& can1)
    : _can0(can0), _can1(can1) {}

CanInterface* CANService::_get(CanBusId bus) {
    if (bus == CAN_BUS_0) return &_can0;
    if (bus == CAN_BUS_1) return &_can1;
    return nullptr;
}

const CanInterface* CANService::_get(CanBusId bus) const {
    if (bus == CAN_BUS_0) return &_can0;
    if (bus == CAN_BUS_1) return &_can1;
    return nullptr;
}

bool CANService::begin() {
    const bool can0Ready = begin(CAN_BUS_0);
    begin(CAN_BUS_1);
    return can0Ready;
}

bool CANService::begin(CanBusId bus) {
    CanInterface* interface = _get(bus);
    return interface && interface->begin();
}

void CANService::end() {
    end(CAN_BUS_0);
    end(CAN_BUS_1);
}

void CANService::end(CanBusId bus) {
    CanInterface* interface = _get(bus);
    if (interface) interface->end();
}

bool CANService::sendMessage(const CanMessage& msg, uint32_t timeout) {
    return _can0.sendMessage(msg, timeout);
}

bool CANService::sendMessage(CanBusId bus, const CanMessage& msg, uint32_t timeout) {
    CanInterface* interface = _get(bus);
    return interface && interface->sendMessage(msg, timeout);
}

bool CANService::receiveMessage(CanMessage& msg, uint32_t timeout) {
    return _can0.receiveMessage(msg, timeout);
}

bool CANService::receiveMessage(CanBusId bus, CanMessage& msg, uint32_t timeout) {
    CanInterface* interface = _get(bus);
    return interface && interface->receiveMessage(msg, timeout);
}

bool CANService::receiveMessageNonBlocking(CanMessage& msg) {
    return _can0.receiveMessageNonBlocking(msg);
}

bool CANService::receiveMessageNonBlocking(CanBusId bus, CanMessage& msg) {
    CanInterface* interface = _get(bus);
    return interface && interface->receiveMessageNonBlocking(msg);
}

void CANService::flushRxQueue() {
    _can0.flushRxQueue();
}

void CANService::flushRxQueue(CanBusId bus) {
    CanInterface* interface = _get(bus);
    if (interface) interface->flushRxQueue();
}

bool CANService::isActive() {
    return _can0.isActive();
}

bool CANService::isActive(CanBusId bus) {
    CanInterface* interface = _get(bus);
    return interface && interface->isActive();
}

CanError CANService::getLastError() {
    return _can0.getLastError();
}

CanError CANService::getLastError(CanBusId bus) {
    CanInterface* interface = _get(bus);
    return interface ? interface->getLastError() : CAN_ERROR_INIT;
}

bool CANService::recoverFromBusOff() {
    return _can0.recoverFromBusOff();
}

bool CANService::recoverFromBusOff(CanBusId bus) {
    CanInterface* interface = _get(bus);
    return interface && interface->recoverFromBusOff();
}

void CANService::getStats(uint32_t& txCount, uint32_t& rxCount,
                          uint32_t& errorCount) {
    _can0.getStats(txCount, rxCount, errorCount);
}

void CANService::getStats(CanBusId bus, uint32_t& txCount, uint32_t& rxCount,
                          uint32_t& errorCount) {
    CanInterface* interface = _get(bus);
    if (interface) interface->getStats(txCount, rxCount, errorCount);
    else txCount = rxCount = errorCount = 0;
}

bool CANService::getDiagnostics(CanDiagnostics& out) {
    return _can0.getDiagnostics(out);
}

bool CANService::getDiagnostics(CanBusId bus, CanDiagnostics& out) {
    CanInterface* interface = _get(bus);
    return interface && interface->getDiagnostics(out);
}

uint32_t CANService::getLastRxTime() const {
    return _can0.getLastRxTime();
}

uint32_t CANService::getLastRxTime(CanBusId bus) const {
    const CanInterface* interface = _get(bus);
    return interface ? interface->getLastRxTime() : 0;
}

bool CANService::reconfigureMode(bool listenOnly) {
    return _can0.reconfigureMode(listenOnly);
}

bool CANService::reconfigureMode(CanBusId bus, bool listenOnly) {
    CanInterface* interface = _get(bus);
    return interface && interface->reconfigureMode(listenOnly);
}

bool CANService::isListenOnlyActive() {
    return _can0.isListenOnlyActive();
}

bool CANService::isListenOnlyActive(CanBusId bus) {
    CanInterface* interface = _get(bus);
    return interface && interface->isListenOnlyActive();
}