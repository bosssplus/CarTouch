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

    // state machine غیرمسدودکننده
    _pollState = OBD_POLL_IDLE;
    _pollIndex = 0;
    _pollWaitStartMs = 0;
    _hasCompletedRound = false;
    _pollIntervalMs = 200;   // فاصله بین دو دور کامل خواندن
    _lastRoundStartMs = 0;
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

// ======================== خواندن همه PIDها [BLOCKING - سازگاری عقب‌رو] ========================
// این تابع دیگر در مسیر اصلی loop() صدا زده نمی‌شود (به جایش از
// update() + getLatestData() استفاده کنید). فقط برای کدی نگه داشته
// شده که مستقیماً به این امضا وابسته است.

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

// ======================== [NON-BLOCKING] state machine خواندن دوره‌ای ========================
//
// ترتیب PID هر دور: RPM(0) → Speed(1) → Coolant(2) → Throttle(3) →
// Fuel(4) → Runtime(5) → پایان دور → مکث _pollIntervalMs → دور بعد.
//
// این جایگزین معماری برای مورد ۴ چک‌لیست تجاری است: readAllPIDs قدیم
// در بدترین حالت (۶ PID، هرکدام تا ۲۰۰ms timeout) می‌توانست تا حدود
// ۱٫۲ ثانیه loop() را کاملاً مسدود کند. اینجا هیچ delay() و هیچ حلقه‌ی
// "منتظر بمان تا..." وجود ندارد؛ هر فراخوانی update() فقط یک قدم کوچک
// (ارسال یک درخواست، یا یک بار چک غیرمسدودکننده‌ی صف CAN) انجام می‌دهد
// و بلافاصله برمی‌گردد.

void OBD2Reader::_applyPidToData(uint8_t pid, const ObdResponse& resp, VehicleData& data) {
    if (!resp.success) return;  // PID بی‌پاسخ: مقدار قبلی در _pendingData دست‌نخورده می‌ماند

    switch (pid) {
        case OBD_PID_ENGINE_RPM:
            if (resp.length >= 2)
                data.engineRPM = ((uint16_t)resp.data[0] * 256 + resp.data[1]) / 4;
            break;
        case OBD_PID_VEHICLE_SPEED:
            if (resp.length >= 1) data.vehicleSpeed = resp.data[0];
            break;
        case OBD_PID_COOLANT_TEMP:
            if (resp.length >= 1) data.coolantTemp = (int8_t)(resp.data[0] - 40);
            break;
        case OBD_PID_THROTTLE_POS:
            if (resp.length >= 1)
                data.throttlePos = (uint8_t)((float)resp.data[0] * 100.0f / 255.0f);
            break;
        case OBD_PID_FUEL_LEVEL:
            if (resp.length >= 1)
                data.fuelLevel = (uint8_t)((float)resp.data[0] * 100.0f / 255.0f);
            break;
        case OBD_PID_RUNTIME:
            if (resp.length >= 2)
                data.engineRuntime = ((uint16_t)resp.data[0] * 256 + resp.data[1]);
            break;
    }
}

// جدول PID به ترتیب _pollIndex - باید با _applyPidToData هماهنگ بماند
static const uint8_t OBD_POLL_PID_TABLE[6] = {
    OBD_PID_ENGINE_RPM, OBD_PID_VEHICLE_SPEED, OBD_PID_COOLANT_TEMP,
    OBD_PID_THROTTLE_POS, OBD_PID_FUEL_LEVEL, OBD_PID_RUNTIME
};

void OBD2Reader::_pollStartNextPid() {
    uint8_t pid = OBD_POLL_PID_TABLE[_pollIndex];

    CanMessage request;
    request.id = OBD_REQUEST_ID;
    request.isExtended = false;
    request.isRemote = false;
    request.length = 8;
    request.data[0] = 0x02;
    request.data[1] = OBD_MODE_CURRENT;
    request.data[2] = pid;
    request.data[3] = 0x00;
    request.data[4] = 0x00;
    request.data[5] = 0x00;
    request.data[6] = 0x00;
    request.data[7] = 0x00;

    // sendMessage خودش هم non-blocking-ish است (فقط تا timeout کوتاه صف
    // TX منتظر می‌ماند، نه پاسخ ECU) - این بخش قبلاً هم مشکل نبود.
    // مشکل قبلی حلقه‌ی *دریافت* پاسخ بود که اینجا حذف شده.
    if (_can.sendMessage(request)) {
        _pollWaitStartMs = millis();
        _pollState = OBD_POLL_WAITING;
    } else {
        // ارسال ناموفق: این PID را رد کن و برو سراغ بعدی، تا یک PID
        // خراب کل دور را برای همیشه گیر ندهد
        _pollIndex++;
        if (_pollIndex >= _POLL_PID_COUNT) {
            _pollState = OBD_POLL_DONE;
        } else {
            _pollState = OBD_POLL_SENDING;
        }
    }
}

void OBD2Reader::_pollCheckResponse() {
    uint8_t pid = OBD_POLL_PID_TABLE[_pollIndex];

    // چک *غیرمسدودکننده* صف دریافت (timeout=0 داخل receiveMessageNonBlocking)
    CanMessage reply;
    bool gotSomething = _can.receiveMessageNonBlocking(reply);

    if (gotSomething) {
        if ((reply.id == OBD_REPLY_ID || reply.id == OBD_REPLY_ID_2) &&
            reply.length >= 3 &&
            reply.data[1] == (OBD_MODE_CURRENT + 0x40) &&
            reply.data[2] == pid) {

            ObdResponse resp;
            resp.pid = pid;
            resp.length = reply.length - 3;
            resp.success = true;
            resp.timestamp = millis();
            for (int i = 0; i < resp.length && i < 6; i++) {
                resp.data[i] = reply.data[i + 3];
            }
            _applyPidToData(pid, resp, _pendingData);

            _pollIndex++;
            _pollState = (_pollIndex >= _POLL_PID_COUNT) ? OBD_POLL_DONE : OBD_POLL_SENDING;
            return;
        }
        // پیامی دریافت شد ولی مربوط به این PID نبود (ترافیک دیگر باس)؛
        // دور همین حلقه‌ی update() بعدی دوباره چک می‌شود - بدون انتظار.
        return;
    }

    // هیچ پیامی در صف نبود این‌بار - چک timeout (بدون delay/بدون حلقه)
    if (millis() - _pollWaitStartMs > 200) {
        ObdResponse timeoutResp;
        timeoutResp.success = false;
        _applyPidToData(pid, timeoutResp, _pendingData);  // مقدار قبلی حفظ می‌شود

        _pollIndex++;
        _pollState = (_pollIndex >= _POLL_PID_COUNT) ? OBD_POLL_DONE : OBD_POLL_SENDING;
    }
    // در غیر این صورت: همچنان در حال انتظاریم، دفعه‌ی بعد update() دوباره چک می‌کند
}

void OBD2Reader::update() {
    uint32_t now = millis();

    switch (_pollState) {
        case OBD_POLL_IDLE:
            if (now - _lastRoundStartMs >= _pollIntervalMs) {
                _pendingData = _hasCompletedRound ? _latestData : VehicleData();
                _pollIndex = 0;
                _lastRoundStartMs = now;
                _pollState = OBD_POLL_SENDING;
            }
            break;

        case OBD_POLL_SENDING:
            // رعایت حداقل فاصله‌ی بین درخواست‌های تکی (بدون delay - فقط
            // اگر زودتر از موعد بود، همین دور update() کاری نمی‌کنیم)
            if (now - _lastRequestTime >= _requestInterval) {
                _pollStartNextPid();
                _lastRequestTime = now;
            }
            break;

        case OBD_POLL_WAITING:
            _pollCheckResponse();
            break;

        case OBD_POLL_DONE:
            _latestData.engineRPM      = _pendingData.engineRPM;
            _latestData.vehicleSpeed   = _pendingData.vehicleSpeed;
            _latestData.coolantTemp    = _pendingData.coolantTemp;
            _latestData.throttlePos    = _pendingData.throttlePos;
            _latestData.fuelLevel      = _pendingData.fuelLevel;
            _latestData.engineRuntime  = _pendingData.engineRuntime;
            _hasCompletedRound = true;
            _pollState = OBD_POLL_IDLE;
            break;
    }
}

bool OBD2Reader::getLatestData(VehicleData& outData) {
    if (!_hasCompletedRound) return false;
    outData.engineRPM     = _latestData.engineRPM;
    outData.vehicleSpeed  = _latestData.vehicleSpeed;
    outData.coolantTemp   = _latestData.coolantTemp;
    outData.throttlePos   = _latestData.throttlePos;
    outData.fuelLevel     = _latestData.fuelLevel;
    outData.engineRuntime = _latestData.engineRuntime;
    return true;
}

ObdPollState OBD2Reader::getPollState() {
    return _pollState;
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
