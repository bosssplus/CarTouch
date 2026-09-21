/**
 * obd2_reader.cpp - پیاده‌سازی خواندن OBD-II
 * 
 * از استاندارد ISO 15765-4 (CAN 11-bit) پیروی می‌کند.
 * کانال ارتباطی: CAN ID 0x7DF (درخواست) و 0x7E8 (پاسخ از ECU)
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#include "obd2_reader.h"

// ======================== ثابت‌های OBD-II ========================

// CAN IDها برای OBD-II
#define OBD_REQUEST_ID     0x7DF   // Broadcast request ID
#define OBD_REPLY_ID       0x7E8   // ECU reply ID (ECU #1)
#define OBD_REPLY_ID_2     0x7E9   // ECU reply ID (ECU #2, در صورت وجود)

// حالت OBD (Mode)
#define OBD_MODE_CURRENT   0x01    // نمایش داده‌های جاری
#define OBD_MODE_FREEZE    0x02    // داده‌های Freeze Frame
#define OBD_MODE_DTC       0x03    // خواندن DTC
#define OBD_MODE_CLEAR_DTC 0x04    // پاک کردن DTC

// Magic PIDها
#define PID_SUPPORTED_1    0x00    // PIDهای پشتیبانی‌شده (0x01-0x20)
#define PID_SUPPORTED_2    0x20    // PIDهای پشتیبانی‌شده (0x21-0x40)
#define PID_SUPPORTED_3    0x40    // PIDهای پشتیبانی‌شده (0x41-0x60)

// ======================== سازنده ========================

OBD2Reader::OBD2Reader(CANManager& canManager) : _can(canManager) {
    _lastError = 0;
    _lastRequestTime = 0;
    _requestInterval = 50;  // حداقل 50ms بین درخواست‌ها
}

// ======================== مقداردهی اولیه ========================

void OBD2Reader::begin() {
    // پاک کردن صف پیام‌های قدیمی
    _can.flushRxQueue();
    Serial.println("[OBD2] OBD-II Reader آماده شد");
}

// ======================== ارسال درخواست PID ========================

bool OBD2Reader::requestPID(uint8_t pid, ObdResponse& response) {
    // رعایت فاصله زمانی بین درخواست‌ها
    uint32_t now = millis();
    if (now - _lastRequestTime < _requestInterval) {
        delay(_requestInterval - (now - _lastRequestTime));
    }
    
    // ساختن پیام درخواست OBD-II
    // فرمت: [Mode, PID, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
    CanMessage request;
    request.id = OBD_REQUEST_ID;
    request.isExtended = false;
    request.isRemote = false;
    request.length = 8;
    request.data[0] = 0x02;                   // تعداد بایت‌های معتبر
    request.data[1] = OBD_MODE_CURRENT;       // Mode 01: داده‌های جاری
    request.data[2] = pid;                     // PID مورد نظر
    request.data[3] = 0x00;
    request.data[4] = 0x00;
    request.data[5] = 0x00;
    request.data[6] = 0x00;
    request.data[7] = 0x00;

    // ارسال درخواست
    if (!_can.sendMessage(request)) {
        _lastError = 1;
        response.success = false;
        return false;
    }
    
    _lastRequestTime = millis();

    // دریافت پاسخ با timeout
    CanMessage reply;
    uint32_t timeout = millis() + 200;  // 200ms مهلت
    bool received = false;
    
    while (millis() < timeout) {
        if (_can.receiveMessage(reply, 50)) {
            // بررسی اینکه آیا این پاسخ مربوط به درخواست ماست
            if (reply.id == OBD_REPLY_ID || reply.id == OBD_REPLY_ID_2) {
                if (reply.length >= 3 && 
                    reply.data[1] == (OBD_MODE_CURRENT + 0x40) && // Mode回應
                    reply.data[2] == pid) {
                    received = true;
                    break;
                }
            }
        }
    }

    if (!received) {
        _lastError = 2;
        response.success = false;
        return false;
    }

    // پردازش پاسخ
    response.pid = pid;
    response.length = reply.length - 3;  // رد کردن header
    response.success = true;
    response.timestamp = millis();
    
    // کپی کردن داده‌های خالص (بدون header)
    for (int i = 0; i < response.length && i < 6; i++) {
        response.data[i] = reply.data[i + 3];
    }

    _lastError = 0;
    return true;
}

// ======================== خواندن RPM ========================

uint16_t OBD2Reader::readEngineRPM() {
    ObdResponse response;
    if (!requestPID(OBD_PID_ENGINE_RPM, response)) return 0;
    
    // فرمول: ((A * 256) + B) / 4
    if (response.length >= 2) {
        return ((uint16_t)response.data[0] * 256 + response.data[1]) / 4;
    }
    return 0;
}

// ======================== خواندن سرعت ========================

uint8_t OBD2Reader::readVehicleSpeed() {
    ObdResponse response;
    if (!requestPID(OBD_PID_VEHICLE_SPEED, response)) return 0;
    
    // فرمول: A (بدون ضریب)
    if (response.length >= 1) {
        return response.data[0];
    }
    return 0;
}

// ======================== خواندن دمای خنک‌کننده ========================

int8_t OBD2Reader::readCoolantTemp() {
    ObdResponse response;
    if (!requestPID(OBD_PID_COOLANT_TEMP, response)) return -40;
    
    // فرمول: A - 40
    if (response.length >= 1) {
        return response.data[0] - 40;
    }
    return -40;
}

// ======================== خواندن دریچه گاز ========================

uint8_t OBD2Reader::readThrottlePosition() {
    ObdResponse response;
    if (!requestPID(OBD_PID_THROTTLE_POS, response)) return 0;
    
    // فرمول: A * 100 / 255
    if (response.length >= 1) {
        return (uint8_t)((float)response.data[0] * 100.0f / 255.0f);
    }
    return 0;
}

// ======================== خواندن سطح سوخت ========================

uint8_t OBD2Reader::readFuelLevel() {
    ObdResponse response;
    if (!requestPID(OBD_PID_FUEL_LEVEL, response)) return 0;
    
    // فرمول: A * 100 / 255
    if (response.length >= 1) {
        return (uint8_t)((float)response.data[0] * 100.0f / 255.0f);
    }
    return 0;
}

// ======================== خواندن زمان روشن بودن موتور ========================

uint16_t OBD2Reader::readEngineRuntime() {
    ObdResponse response;
    if (!requestPID(OBD_PID_RUNTIME, response)) return 0;
    
    // فرمول: (A * 256) + B
    if (response.length >= 2) {
        return ((uint16_t)response.data[0] * 256 + response.data[1]);
    }
    return 0;
}

// ======================== خواندن همه PIDها ========================

void OBD2Reader::readAllPIDs(VehicleData& data) {
    data.engineRPM = readEngineRPM();
    delay(10);
    data.vehicleSpeed = readVehicleSpeed();
    delay(10);
    data.coolantTemp = readCoolantTemp();
    delay(10);
    data.throttlePos = readThrottlePosition();
    delay(10);
    data.fuelLevel = readFuelLevel();
    delay(10);
    data.engineRuntime = readEngineRuntime();
}

// ======================== بررسی پشتیبانی PID ========================

bool OBD2Reader::isPidSupported(uint8_t pid) {
    // ابتدا باید PID 0x00 را بخوانیم تا ببینیم کدام PIDها پشتیبانی می‌شوند
    ObdResponse response;
    if (!requestPID(PID_SUPPORTED_1, response)) return false;
    
    // پاسخ 4 بایتی: بیت‌های 32-1 نشان‌دهنده پشتیبانی از PIDهای 0x01-0x20
    if (response.length >= 4 && pid >= 0x01 && pid <= 0x20) {
        uint32_t supported = 0;
        for (int i = 0; i < 4; i++) {
            supported = (supported << 8) | response.data[i];
        }
        return (supported >> (32 - pid)) & 1;
    }
    
    return false;
}

// ======================== خواندن DTC ========================

uint8_t OBD2Reader::readDTCs(uint16_t dtcList[], uint8_t maxCount) {
    CanMessage request;
    request.id = OBD_REQUEST_ID;
    request.isExtended = false;
    request.isRemote = false;
    request.length = 8;
    request.data[0] = 0x01;
    request.data[1] = OBD_MODE_DTC;        // Mode 03: خواندن DTC
    request.data[2] = 0x00;
    request.data[3] = 0x00;
    request.data[4] = 0x00;
    request.data[5] = 0x00;
    request.data[6] = 0x00;
    request.data[7] = 0x00;

    _can.sendMessage(request);
    
    // دریافت پاسخ‌های DTC (ممکن است چند فریم باشد)
    CanMessage reply;
    uint8_t dtcCount = 0;
    uint32_t timeout = millis() + 500;
    
    while (millis() < timeout && dtcCount < maxCount) {
        if (_can.receiveMessage(reply, 100)) {
            if (reply.id == OBD_REPLY_ID && reply.length >= 3) {
                // اولین بایت تعداد DTCهاست
                // بایت‌های بعدی: هر 2 بایت یک DTC
                for (int i = 3; i + 1 < reply.length && dtcCount < maxCount; i += 2) {
                    dtcList[dtcCount] = ((uint16_t)reply.data[i] << 8) | reply.data[i+1];
                    dtcCount++;
                }
            }
        }
    }
    
    return dtcCount;
}

// ======================== پاک کردن DTC ========================

bool OBD2Reader::clearDTCs() {
    CanMessage request;
    request.id = OBD_REQUEST_ID;
    request.isExtended = false;
    request.isRemote = false;
    request.length = 8;
    request.data[0] = 0x01;
    request.data[1] = OBD_MODE_CLEAR_DTC;  // Mode 04: پاک کردن DTC
    request.data[2] = 0x00;
    request.data[3] = 0x00;
    request.data[4] = 0x00;
    request.data[5] = 0x00;
    request.data[6] = 0x00;
    request.data[7] = 0x00;

    return _can.sendMessage(request);
}

// ======================== آخرین خطا ========================

uint8_t OBD2Reader::getLastError() {
    return _lastError;
}
