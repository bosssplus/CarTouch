/**
 * vehicle_control.cpp - پیاده‌سازی کنترل خودرو (CarTouch v2.0)
 * 
 * تفاوت اصلی با v1.0: به‌جای CAN ID های هاردکد، فرمان‌ها از
 * ActiveProfileManager (که خودش از VehicleDB یا CustomVehicleStore
 * می‌خواند) حل می‌شوند. به بخش ۷.۱ سند مراجعه کنید.
 * 
 * rate-limit سطح پایین (MIN_COMMAND_INTERVAL_MS=150ms) دقیقاً مثل
 * v1.0 حفظ شده و هم مسیر جدید (ActiveProfileManager) و هم مسیر
 * legacy (setCustomCANIDs) از همان یک نقطه (_sendResolvedMessage/
 * _sendLegacyCommand که هر دو _lastCommandTime مشترک را چک می‌کنند)
 * عبور می‌کنند.
 */

#include "vehicle_control.h"
#include "custom_vehicle.h"

// ======================== سازنده ========================

VehicleControl::VehicleControl(CANManager& canManager, ActiveProfileManager& profileManager)
    : _can(canManager), _profileManager(profileManager) {
    _lastError = 0;
    _lastErrorMessage = "";
    _lastCommandTime = 0;
    
    _legacyOverrideActive = false;
    _canIdDoorLock = 0x1A0;
    _canIdWindow   = 0x1A1;
    _canIdSunroof  = 0x1A2;
    _canIdTrunk    = 0x1A3;
    _canIdMirror   = 0x1A4;
    _canIdAlarm    = 0x1A5;
}

// ======================== مقداردهی اولیه ========================

void VehicleControl::begin() {
    Serial.println("[CTRL] Vehicle Control آماده شد (v2.0 - profile-driven)");
}

// ======================== ارسال پیام حل‌شده (مسیر جدید) ========================

bool VehicleControl::_sendResolvedMessage(const CanMessage& msg) {
    if (getConfig()->listenOnlyMode) {
        Serial.println("⚠️ [CTRL] حالت Listen-Only فعال است - دستور ارسال نشد");
        _lastError = 1;
        _lastErrorMessage = "حالت Listen-Only فعال است";
        return false;
    }
    
    // اصلاحیه (حفظ‌شده از v1.0): جلوگیری از ارسال فرمان‌های فیزیکی
    // با سرعت خیلی بالا که می‌تواند به موتورهای شیشه/قفل درب فشار
    // مکانیکی غیرعادی وارد کند.
    uint32_t now = millis();
    if (now - _lastCommandTime < MIN_COMMAND_INTERVAL_MS) {
        Serial.println("⚠️ [CTRL] فرمان خیلی سریع پشت‌سرهم - نادیده گرفته شد (rate limit)");
        _lastError = 3;
        _lastErrorMessage = "فرمان‌ها خیلی سریع پشت‌سرهم ارسال شدند (rate limit)";
        return false;
    }
    _lastCommandTime = now;
    
    if (_can.sendMessage(msg)) {
        Serial.printf("[CTRL] فرمان ارسال شد: ID=0x%03X, داده=", msg.id);
        for (int i = 0; i < msg.length; i++) {
            Serial.printf("%02X ", msg.data[i]);
        }
        Serial.println();
        _lastError = 0;
        _lastErrorMessage = "";
        return true;
    }
    
    _lastError = 2;
    _lastErrorMessage = "ارسال روی CAN Bus ناموفق بود";
    return false;
}

// ======================== ارسال legacy (سازگاری عقب‌رو) ========================

bool VehicleControl::_sendLegacyCommand(uint32_t canId, const uint8_t* data, uint8_t length) {
    CanMessage msg;
    msg.id = canId;
    msg.isExtended = false;
    msg.isRemote = false;
    msg.length = length;
    for (int i = 0; i < length && i < 8; i++) {
        msg.data[i] = data[i];
    }
    return _sendResolvedMessage(msg);
}

// ======================== اجرای یک برچسب ========================

bool VehicleControl::_execute(const char* label, String& outErrorReason) {
    if (_legacyOverrideActive) {
        // مسیر سازگاری عقب‌رو: نگاشت برچسب‌های شناخته‌شده به CAN
        // ID های legacy تنظیم‌شده با setCustomCANIDs(). این مسیر
        // چرخه‌ی تأیید UNVERIFIED/VERIFIED را ندارد چون از قبل (در
        // v1.0) هم نداشت - برای همین در مستندات به کاربر توصیه شده
        // به‌جای این تابع از ورود دستی جدید استفاده کند.
        if (strcmp(label, CMD_LABEL_LOCK_ALL) == 0) {
            uint8_t d[] = {0x01};
            return _sendLegacyCommand(_canIdDoorLock, d, 1);
        } else if (strcmp(label, CMD_LABEL_UNLOCK_ALL) == 0) {
            uint8_t d[] = {0x02};
            return _sendLegacyCommand(_canIdDoorLock, d, 1);
        } else if (strcmp(label, CMD_LABEL_UNLOCK_DRIVER) == 0) {
            uint8_t d[] = {0x04};
            return _sendLegacyCommand(_canIdDoorLock, d, 1);
        } else if (strcmp(label, CMD_LABEL_ALL_WINDOWS_UP) == 0) {
            uint8_t d[] = {0xFF, 0x01};
            return _sendLegacyCommand(_canIdWindow, d, 2);
        } else if (strcmp(label, CMD_LABEL_ALL_WINDOWS_DOWN) == 0) {
            uint8_t d[] = {0xFF, 0x02};
            return _sendLegacyCommand(_canIdWindow, d, 2);
        } else if (strcmp(label, CMD_LABEL_SUNROOF_OPEN) == 0) {
            uint8_t d[] = {0x01};
            return _sendLegacyCommand(_canIdSunroof, d, 1);
        } else if (strcmp(label, CMD_LABEL_SUNROOF_CLOSE) == 0) {
            uint8_t d[] = {0x02};
            return _sendLegacyCommand(_canIdSunroof, d, 1);
        } else if (strcmp(label, CMD_LABEL_SUNROOF_TILT) == 0) {
            uint8_t d[] = {0x03};
            return _sendLegacyCommand(_canIdSunroof, d, 1);
        } else if (strcmp(label, CMD_LABEL_TRUNK_OPEN) == 0) {
            uint8_t d[] = {0x01};
            return _sendLegacyCommand(_canIdTrunk, d, 1);
        } else if (strcmp(label, CMD_LABEL_TRUNK_LOCK) == 0) {
            uint8_t d[] = {0x02};
            return _sendLegacyCommand(_canIdTrunk, d, 1);
        } else if (strcmp(label, CMD_LABEL_MIRROR_FOLD) == 0) {
            uint8_t d[] = {0x01};
            return _sendLegacyCommand(_canIdMirror, d, 1);
        } else if (strcmp(label, CMD_LABEL_MIRROR_UNFOLD) == 0) {
            uint8_t d[] = {0x02};
            return _sendLegacyCommand(_canIdMirror, d, 1);
        } else if (strcmp(label, CMD_LABEL_ALARM_ARM) == 0) {
            uint8_t d[] = {0x01};
            return _sendLegacyCommand(_canIdAlarm, d, 1);
        } else if (strcmp(label, CMD_LABEL_ALARM_DISARM) == 0) {
            uint8_t d[] = {0x02};
            return _sendLegacyCommand(_canIdAlarm, d, 1);
        } else if (strncmp(label, "window_", 7) == 0) {
            // window_fl_up, window_fr_down, ...
            uint8_t winIdx = 0xFF;
            bool up = strstr(label, "_up") != nullptr;
            if (strstr(label, "fl")) winIdx = 0;
            else if (strstr(label, "fr")) winIdx = 1;
            else if (strstr(label, "rl")) winIdx = 2;
            else if (strstr(label, "rr")) winIdx = 3;
            if (winIdx == 0xFF) {
                outErrorReason = "شناسه شیشه در برچسب legacy قابل تشخیص نبود";
                return false;
            }
            uint8_t d[] = {winIdx, (uint8_t)(up ? 0x01 : 0x02)};
            return _sendLegacyCommand(_canIdWindow, d, 2);
        }
        
        outErrorReason = "این برچسب در مسیر سازگاری legacy پشتیبانی نمی‌شود";
        return false;
    }
    
    // === مسیر اصلی v2.0: از ActiveProfileManager بخواه ===
    CanMessage msg;
    if (!_profileManager.resolveCommand(label, msg, outErrorReason)) {
        return false;
    }
    
    return _sendResolvedMessage(msg);
}

// ======================== executeCommand (عمومی) ========================

bool VehicleControl::executeCommand(const char* commandLabel) {
    String reason;
    return executeCommand(commandLabel, reason);
}

bool VehicleControl::executeCommand(const char* commandLabel, String& outErrorReason) {
    bool result = _execute(commandLabel, outErrorReason);
    if (!result && outErrorReason.length() > 0) {
        _lastErrorMessage = outErrorReason;
        Serial.printf("⚠️ [CTRL] اجرای فرمان '%s' ناموفق: %s\n", 
                      commandLabel, outErrorReason.c_str());
    }
    return result;
}

// ======================== توابع سازگاری v1.0 ========================

bool VehicleControl::lockAllDoors() {
    return executeCommand(CMD_LABEL_LOCK_ALL);
}

bool VehicleControl::unlockAllDoors() {
    return executeCommand(CMD_LABEL_UNLOCK_ALL);
}

bool VehicleControl::unlockDriverDoor() {
    return executeCommand(CMD_LABEL_UNLOCK_DRIVER);
}

bool VehicleControl::windowUp(uint8_t window) {
    if (window > 3) return false;
    static const char* labels[4] = {
        CMD_LABEL_WINDOW_FL_UP, CMD_LABEL_WINDOW_FR_UP,
        CMD_LABEL_WINDOW_RL_UP, CMD_LABEL_WINDOW_RR_UP
    };
    return executeCommand(labels[window]);
}

bool VehicleControl::windowDown(uint8_t window) {
    if (window > 3) return false;
    static const char* labels[4] = {
        CMD_LABEL_WINDOW_FL_DOWN, CMD_LABEL_WINDOW_FR_DOWN,
        CMD_LABEL_WINDOW_RL_DOWN, CMD_LABEL_WINDOW_RR_DOWN
    };
    return executeCommand(labels[window]);
}

bool VehicleControl::allWindowsUp() {
    return executeCommand(CMD_LABEL_ALL_WINDOWS_UP);
}

bool VehicleControl::allWindowsDown() {
    return executeCommand(CMD_LABEL_ALL_WINDOWS_DOWN);
}

bool VehicleControl::sunroofOpen() {
    return executeCommand(CMD_LABEL_SUNROOF_OPEN);
}

bool VehicleControl::sunroofClose() {
    return executeCommand(CMD_LABEL_SUNROOF_CLOSE);
}

bool VehicleControl::sunroofTilt() {
    return executeCommand(CMD_LABEL_SUNROOF_TILT);
}

bool VehicleControl::trunkOpen() {
    return executeCommand(CMD_LABEL_TRUNK_OPEN);
}

bool VehicleControl::trunkLock() {
    return executeCommand(CMD_LABEL_TRUNK_LOCK);
}

bool VehicleControl::foldMirrors() {
    return executeCommand(CMD_LABEL_MIRROR_FOLD);
}

bool VehicleControl::unfoldMirrors() {
    return executeCommand(CMD_LABEL_MIRROR_UNFOLD);
}

bool VehicleControl::alarmArm() {
    return executeCommand(CMD_LABEL_ALARM_ARM);
}

bool VehicleControl::alarmDisarm() {
    return executeCommand(CMD_LABEL_ALARM_DISARM);
}

// ======================== توقف همه ========================

bool VehicleControl::stopAll() {
    // best-effort: فقط در حالت legacy override معنای مستقیم دارد
    // (طبق مستندات کلاس). اگر در حالت پروفایل جدید هستیم و برچسب
    // مخصوصی برای توقف یادگرفته نشده، این تابع false برمی‌گرداند
    // به‌جای حدس زدن یک CAN ID.
    if (!_legacyOverrideActive) {
        _lastErrorMessage = "توقف عمومی فقط در حالت legacy یا با فرمان‌های "
                            "مخصوص یادگرفته‌شده پشتیبانی می‌شود";
        return false;
    }
    
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    bool result = true;
    result &= _sendLegacyCommand(_canIdDoorLock, data, 8);
    result &= _sendLegacyCommand(_canIdWindow, data, 8);
    result &= _sendLegacyCommand(_canIdSunroof, data, 8);
    result &= _sendLegacyCommand(_canIdTrunk, data, 8);
    return result;
}

// ======================== تنظیم CAN IDهای سفارشی (legacy) ========================

void VehicleControl::setCustomCANIDs(uint32_t doorLock, uint32_t window,
                                      uint32_t sunroof, uint32_t trunk,
                                      uint32_t mirror, uint32_t alarm) {
    _canIdDoorLock = doorLock;
    _canIdWindow   = window;
    _canIdSunroof  = sunroof;
    _canIdTrunk    = trunk;
    _canIdMirror   = mirror;
    _canIdAlarm    = alarm;
    _legacyOverrideActive = true;
    
    Serial.println("[CTRL] حالت override دستی legacy فعال شد - CAN IDهای سفارشی:");
    Serial.printf("  DoorLock=0x%03X, Window=0x%03X, Sunroof=0x%03X\n", 
                  doorLock, window, sunroof);
    Serial.printf("  Trunk=0x%03X, Mirror=0x%03X, Alarm=0x%03X\n",
                  trunk, mirror, alarm);
    Serial.println("⚠️ [CTRL] توجه: مسیر legacy از چرخه‌ی تأیید UNVERIFIED/VERIFIED "
                   "عبور نمی‌کند. برای ایمنی بیشتر، «ورود دستی» در بخش «فرمان‌های من» را ترجیح دهید.");
}

// ======================== آخرین خطا ========================

uint8_t VehicleControl::getLastError() {
    return _lastError;
}

String VehicleControl::getLastErrorMessage() {
    return _lastErrorMessage;
}
