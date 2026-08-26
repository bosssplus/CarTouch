/**
 * vehicle_control.cpp - پیاده‌سازی کنترل خودرو
 * 
 * تمام توابع این فایل تست شده (روی شبیه‌ساز CAN) ولی مقادیر پیش‌فرض
 * DBC ممکن است برای خودروی شما نیاز به تنظیم داشته باشد.
 */

#include "vehicle_control.h"

// ======================== سازنده ========================

VehicleControl::VehicleControl(CANManager& canManager) : _can(canManager) {
    _lastError = 0;
    
    // CAN IDهای پیش‌فرض (بر اساس خودروهای رایج)
    _canIdDoorLock = 0x1A0;
    _canIdWindow   = 0x1A1;
    _canIdSunroof  = 0x1A2;
    _canIdTrunk    = 0x1A3;
    _canIdMirror   = 0x1A4;
    _canIdAlarm    = 0x1A5;
}

// ======================== مقداردهی اولیه ========================

void VehicleControl::begin() {
    Serial.println("[CTRL] Vehicle Control آماده شد");
}

// ======================== تابع داخلی ارسال فرمان ========================

bool VehicleControl::_sendCommand(uint32_t canId, const uint8_t* data, uint8_t length) {
    if (getConfig()->listenOnlyMode) {
        Serial.println("⚠️ [CTRL] حالت Listen-Only فعال است - دستور ارسال نشد");
        _lastError = 1;
        return false;
    }
    
    CanMessage msg;
    msg.id = canId;
    msg.isExtended = false;
    msg.isRemote = false;
    msg.length = length;
    
    for (int i = 0; i < length && i < 8; i++) {
        msg.data[i] = data[i];
    }
    
    if (_can.sendMessage(msg)) {
        Serial.printf("[CTRL] فرمان ارسال شد: ID=0x%03X, داده=", canId);
        for (int i = 0; i < length; i++) {
            Serial.printf("%02X ", data[i]);
        }
        Serial.println();
        _lastError = 0;
        return true;
    }
    
    _lastError = 2;
    return false;
}

// ======================== قفل درب‌ها ========================

bool VehicleControl::lockAllDoors() {
    // فرمت رایج: [0x01] = قفل همه درب‌ها
    uint8_t data[] = {0x01};
    return _sendCommand(_canIdDoorLock, data, 1);
}

bool VehicleControl::unlockAllDoors() {
    // فرمت رایج: [0x02] = باز کردن قفل همه درب‌ها
    uint8_t data[] = {0x02};
    return _sendCommand(_canIdDoorLock, data, 1);
}

bool VehicleControl::unlockDriverDoor() {
    // فرمت رایج: [0x04] = فقط درب راننده
    uint8_t data[] = {0x04};
    return _sendCommand(_canIdDoorLock, data, 1);
}

// ======================== شیشه‌ها ========================

bool VehicleControl::windowUp(uint8_t window) {
    if (window > 3) return false;
    // فرمت: [window_id, 0x01] = بالا
    uint8_t data[] = {window, 0x01};
    return _sendCommand(_canIdWindow, data, 2);
}

bool VehicleControl::windowDown(uint8_t window) {
    if (window > 3) return false;
    // فرمت: [window_id, 0x02] = پایین
    uint8_t data[] = {window, 0x02};
    return _sendCommand(_canIdWindow, data, 2);
}

bool VehicleControl::allWindowsUp() {
    uint8_t data[] = {0xFF, 0x01};
    return _sendCommand(_canIdWindow, data, 2);
}

bool VehicleControl::allWindowsDown() {
    uint8_t data[] = {0xFF, 0x02};
    return _sendCommand(_canIdWindow, data, 2);
}

// ======================== سانروف ========================

bool VehicleControl::sunroofOpen() {
    uint8_t data[] = {0x01}; // باز کردن
    return _sendCommand(_canIdSunroof, data, 1);
}

bool VehicleControl::sunroofClose() {
    uint8_t data[] = {0x02}; // بستن
    return _sendCommand(_canIdSunroof, data, 1);
}

bool VehicleControl::sunroofTilt() {
    uint8_t data[] = {0x03}; // کج کردن
    return _sendCommand(_canIdSunroof, data, 1);
}

// ======================== صندوق عقب ========================

bool VehicleControl::trunkOpen() {
    uint8_t data[] = {0x01}; // باز کردن
    return _sendCommand(_canIdTrunk, data, 1);
}

bool VehicleControl::trunkLock() {
    uint8_t data[] = {0x02}; // قفل کردن
    return _sendCommand(_canIdTrunk, data, 1);
}

// ======================== آینه‌ها ========================

bool VehicleControl::foldMirrors() {
    uint8_t data[] = {0x01}; // تا کردن
    return _sendCommand(_canIdMirror, data, 1);
}

bool VehicleControl::unfoldMirrors() {
    uint8_t data[] = {0x02}; // باز کردن
    return _sendCommand(_canIdMirror, data, 1);
}

// ======================== دزدگیر ========================

bool VehicleControl::alarmArm() {
    uint8_t data[] = {0x01}; // فعال کردن
    return _sendCommand(_canIdAlarm, data, 1);
}

bool VehicleControl::alarmDisarm() {
    uint8_t data[] = {0x02}; // غیرفعال کردن
    return _sendCommand(_canIdAlarm, data, 1);
}

// ======================== توقف همه ========================

bool VehicleControl::stopAll() {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    bool result = true;
    
    // ارسال دستور توقف به همه ماژول‌ها
    result &= _sendCommand(_canIdDoorLock, data, 8);
    result &= _sendCommand(_canIdWindow, data, 8);
    result &= _sendCommand(_canIdSunroof, data, 8);
    result &= _sendCommand(_canIdTrunk, data, 8);
    
    return result;
}

// ======================== تنظیم CAN IDهای سفارشی ========================

void VehicleControl::setCustomCANIDs(uint32_t doorLock, uint32_t window, 
                                      uint32_t sunroof, uint32_t trunk,
                                      uint32_t mirror, uint32_t alarm) {
    _canIdDoorLock = doorLock;
    _canIdWindow   = window;
    _canIdSunroof  = sunroof;
    _canIdTrunk    = trunk;
    _canIdMirror   = mirror;
    _canIdAlarm    = alarm;
    
    Serial.println("[CTRL] CAN IDهای سفارشی تنظیم شدند:");
    Serial.printf("  DoorLock=0x%03X, Window=0x%03X, Sunroof=0x%03X\n", 
                  doorLock, window, sunroof);
    Serial.printf("  Trunk=0x%03X, Mirror=0x%03X, Alarm=0x%03X\n",
                  trunk, mirror, alarm);
}

// ======================== آخرین خطا ========================

uint8_t VehicleControl::getLastError() {
    return _lastError;
}
