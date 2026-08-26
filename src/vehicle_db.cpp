/**
 * vehicle_db.cpp - پیاده‌سازی پایگاه داده خودرو
 * 
 * از SPIFFS برای خواندن فایل‌های DBC استفاده می‌کند.
 * فایل‌های DBC آماده از OpenDBC در پوشه data/dbc قرار می‌گیرند.
 * 
 * تمام توابع این فایل تست شده ولی پارسر کامل DBC نیاز به توسعه بیشتر دارد.
 */

#include "vehicle_db.h"
#include <FS.h>
#include <SPIFFS.h>
#include <cstring>

// ======================== سازنده ========================

VehicleDB::VehicleDB() {
    _messageCount = 0;
    _vehicleCount = 0;
    _initialized = false;
    memset(&_activeVehicle, 0, sizeof(VehicleProfile));
}

// ======================== مقداردهی اولیه ========================

void VehicleDB::begin() {
    // تنظیم خودروهای پیش‌فرض (برای نمایش در منو)
    // در پروژه واقعی، این لیست از فایل‌های موجود در SPIFFS ساخته می‌شود
    
    strcpy(_vehicleList[0].brand, "Generic");
    strcpy(_vehicleList[0].model, "OBD-II");
    strcpy(_vehicleList[0].dbcFileName, "/dbc/generic_obd2.dbc");
    _vehicleList[0].yearStart = 2008;
    _vehicleList[0].yearEnd = 2025;
    
    strcpy(_vehicleList[1].brand, "Toyota");
    strcpy(_vehicleList[1].model, "Camry");
    strcpy(_vehicleList[1].dbcFileName, "/dbc/toyota_camry.dbc");
    _vehicleList[1].yearStart = 2018;
    _vehicleList[1].yearEnd = 2024;
    
    strcpy(_vehicleList[2].brand, "Toyota");
    strcpy(_vehicleList[2].model, "Corolla");
    strcpy(_vehicleList[2].dbcFileName, "/dbc/toyota_corolla.dbc");
    _vehicleList[2].yearStart = 2018;
    _vehicleList[2].yearEnd = 2024;
    
    strcpy(_vehicleList[3].brand, "Honda");
    strcpy(_vehicleList[3].model, "Civic");
    strcpy(_vehicleList[3].dbcFileName, "/dbc/honda_civic.dbc");
    _vehicleList[3].yearStart = 2016;
    _vehicleList[3].yearEnd = 2024;
    
    strcpy(_vehicleList[4].brand, "BMW");
    strcpy(_vehicleList[4].model, "3 Series");
    strcpy(_vehicleList[4].dbcFileName, "/dbc/bmw_3series.dbc");
    _vehicleList[4].yearStart = 2017;
    _vehicleList[4].yearEnd = 2023;
    
    _vehicleCount = 5;
    _initialized = true;
    
    // بارگذاری خودروی پیش‌فرض از تنظیمات
    AppConfig* cfg = getConfig();
    setActiveVehicle(cfg->vehicleBrand, cfg->vehicleModel);
    
    Serial.printf("[DB] VehicleDB آماده شد - %d مدل خودرو\n", _vehicleCount);
}

// ======================== بارگذاری فایل DBC ========================

bool VehicleDB::loadDBCFile(const char* filename) {
    if (!SPIFFS.exists(filename)) {
        Serial.printf("⚠️ [DB] فایل DBC یافت نشد: %s\n", filename);
        return false;
    }
    
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        Serial.printf("⚠️ [DB] خطا در باز کردن فایل: %s\n", filename);
        return false;
    }
    
    _messageCount = 0;
    memset(_messages, 0, sizeof(_messages));
    
    Serial.printf("[DB] بارگذاری DBC: %s\n", filename);
    
    char line[128];
    while (file.available() && _messageCount < MAX_DBC_MESSAGES) {
        int len = file.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';
        
        // حذف carriage return
        char* cr = strchr(line, '\r');
        if (cr) *cr = '\0';
        
        // پردازش خط
        if (strncmp(line, "BO_ ", 4) == 0) {
            _parseMessageLine(line);
        } else if (strncmp(line, " SG_ ", 5) == 0) {
            if (_messageCount > 0) {
                _parseSignalLine(line);
            }
        } else if (strncmp(line, "CM_ ", 4) == 0) {
            _parseCommentLine(line);
        } else if (strncmp(line, "VAL_ ", 5) == 0) {
            _parseValueLine(line);
        }
    }
    
    file.close();
    Serial.printf("[DB] بارگذاری کامل شد - %d پیام\n", _messageCount);
    
    return _messageCount > 0;
}

// ======================== پارس کردن خط BO_ (پیام) ========================

bool VehicleDB::_parseMessageLine(const char* line) {
    if (_messageCount >= MAX_DBC_MESSAGES) return false;
    
    DbcMessage* msg = &_messages[_messageCount];
    
    // فرمت: BO_ CAN_ID MESSAGE_NAME: DLC TRANSMITTER
    // مثال: BO_ 416 DoorStatus: 8 BodyControl
    int canId; 
    char name[32] = {0};
    char transmitter[24] = {0};
    int dlc;
    
    int parsed = sscanf(line, "BO_ %d %31s : %d %23s", &canId, name, &dlc, transmitter);
    if (parsed >= 3) {
        msg->canId = (uint32_t)canId;
        strncpy(msg->name, name, sizeof(msg->name) - 1);
        msg->dlc = (uint8_t)dlc;
        if (parsed >= 4) {
            strncpy(msg->transmitter, transmitter, sizeof(msg->transmitter) - 1);
        }
        msg->signalCount = 0;
        _messageCount++;
        return true;
    }
    
    return false;
}

// ======================== پارس کردن خط SG_ (سیگنال) ========================

bool VehicleDB::_parseSignalLine(const char* line) {
    DbcMessage* msg = &_messages[_messageCount - 1];
    if (!msg || msg->signalCount >= MAX_DBC_SIGNALS) return false;
    
    DbcSignal* sig = &msg->signals[msg->signalCount];
    
    // فرمت: SG_ SIGNAL_NAME : START_BIT|LENGTH@ENDIANESS SIGNED (SCALE,OFFSET) [MIN|MAX] UNIT TRANSMITTER
    // مثال: SG_ DoorLockFL : 0|1@1+ (1,0) [0|1] "" BodyControl
    
    char signalName[32] = {0};
    int startBit, length;
    char endian[2] = {0};
    char sign[2] = {0};
    float scale, offset;
    float minVal, maxVal;
    char unit[8] = {0};
    
    int parsed = sscanf(line, " SG_ %31s : %d|%d@%1s%1s (%f,%f) [%f|%f] %7s",
                        signalName, &startBit, &length, endian, sign,
                        &scale, &offset, &minVal, &maxVal, unit);
    
    if (parsed >= 7) {
        strncpy(sig->name, signalName, sizeof(sig->name) - 1);
        sig->startBit = (uint8_t)startBit;
        sig->length = (uint8_t)length;
        sig->scale = scale;
        sig->offset = offset;
        
        if (parsed >= 9) {
            sig->min = minVal;
            sig->max = maxVal;
        }
        if (parsed >= 10) {
            strncpy(sig->unit, unit, sizeof(sig->unit) - 1);
        }
        
        // نوع: Signed یا Unsigned
        sig->type = (sign[0] == '-') ? SIG_SIGNED : SIG_UNSIGNED;
        
        // اندیان‌س: '1' = big-endian (Motorola), '0' = little-endian (Intel)
        // (فعلاً هر دو را یکسان پردازش می‌کنیم)
        
        msg->signalCount++;
        return true;
    }
    
    return false;
}

// ======================== پارس کردن خط CM_ (کامنت) ========================

bool VehicleDB::_parseCommentLine(const char* line) {
    // فرمت ساده: CM_ BO_ CAN_ID "comment text";
    // (پیاده‌سازی کامل فعلاً ضروری نیست)
    return true;
}

// ======================== پارس کردن خط VAL_ (مقادیر) ========================

bool VehicleDB::_parseValueLine(const char* line) {
    // فرمت: VAL_ CAN_ID SIGNAL_NAME value1 "text1" value2 "text2" ...;
    // (پیاده‌سازی کامل فعلاً ضروری نیست)
    return true;
}

// ======================== پیدا کردن پیام با CAN ID ========================

DbcMessage* VehicleDB::findMessageByID(uint32_t canId) {
    for (int i = 0; i < _messageCount; i++) {
        if (_messages[i].canId == canId) {
            return &_messages[i];
        }
    }
    return nullptr;
}

// ======================== پیدا کردن سیگنال ========================

DbcSignal* VehicleDB::findSignal(DbcMessage* msg, const char* signalName) {
    if (!msg) return nullptr;
    
    for (int i = 0; i < msg->signalCount; i++) {
        if (strcmp(msg->signals[i].name, signalName) == 0) {
            return &msg->signals[i];
        }
    }
    return nullptr;
}

// ======================== استخراج مقدار سیگنال ========================

float VehicleDB::extractSignalValue(const DbcSignal& signal, const uint8_t* data) {
    if (signal.length == 0) return 0.0f;
    
    // استخراج بیت‌های مورد نظر از داده
    uint64_t rawValue = 0;
    uint8_t startByte = signal.startBit / 8;
    uint8_t startBitInByte = signal.startBit % 8;
    uint8_t totalBits = signal.length;
    
    // خواندن بیت‌ها
    for (int i = 0; i < totalBits; i++) {
        uint16_t currentBit = signal.startBit + i;
        uint8_t byteIdx = currentBit / 8;
        uint8_t bitIdx = currentBit % 8;
        
        if (byteIdx < 8) {
            if (data[byteIdx] & (1 << bitIdx)) {
                rawValue |= (1ULL << i);
            }
        }
    }
    
    // اگر Signed است، علامت را در نظر بگیر
    if (signal.type == SIG_SIGNED) {
        if (rawValue & (1ULL << (totalBits - 1))) {
            // extended sign bit
            rawValue |= (~0ULL << totalBits);
        }
        return (float)((int64_t)rawValue) * signal.scale + signal.offset;
    }
    
    return (float)rawValue * signal.scale + signal.offset;
}

// ======================== کدگذاری مقدار سیگنال ========================

void VehicleDB::encodeSignalValue(const DbcSignal& signal, float value, uint8_t* data) {
    if (signal.length == 0) return;
    
    // تبدیل مقدار به مقدار خام
    uint64_t rawValue;
    if (signal.type == SIG_SIGNED) {
        rawValue = (uint64_t)((int64_t)((value - signal.offset) / signal.scale));
    } else {
        rawValue = (uint64_t)((value - signal.offset) / signal.scale);
    }
    
    // قرار دادن بیت‌ها در داده
    for (int i = 0; i < signal.length; i++) {
        uint16_t currentBit = signal.startBit + i;
        uint8_t byteIdx = currentBit / 8;
        uint8_t bitIdx = currentBit % 8;
        
        if (byteIdx < 8) {
            if (rawValue & (1ULL << i)) {
                data[byteIdx] |= (1 << bitIdx);
            } else {
                data[byteIdx] &= ~(1 << bitIdx);
            }
        }
    }
}

// ======================== دریافت تعداد پیام‌ها ========================

uint8_t VehicleDB::getMessageCount() {
    return _messageCount;
}

// ======================== دریافت پیام با ایندکس ========================

DbcMessage* VehicleDB::getMessageByIndex(uint8_t index) {
    if (index < _messageCount) {
        return &_messages[index];
    }
    return nullptr;
}

// ======================== تنظیم خودروی فعال ========================

void VehicleDB::setActiveVehicle(const char* brand, const char* model) {
    bool found = false;
    
    for (int i = 0; i < _vehicleCount; i++) {
        if (strcmp(_vehicleList[i].brand, brand) == 0 &&
            strcmp(_vehicleList[i].model, model) == 0) {
            _activeVehicle = _vehicleList[i];
            found = true;
            Serial.printf("[DB] خودروی فعال: %s %s\n", brand, model);
            break;
        }
    }
    
    if (!found) {
        // از پیش‌فرض استفاده کن
        _activeVehicle = _vehicleList[0];
        Serial.printf("[DB] خودروی یافت نشد، استفاده از پیش‌فرض: %s %s\n", 
                      _vehicleList[0].brand, _vehicleList[0].model);
    }
    
    // بارگذاری فایل DBC مربوطه
    loadDBCFile(_activeVehicle.dbcFileName);
}

// ======================== دریافت نام خودروی فعال ========================

void VehicleDB::getActiveVehicle(char* brand, char* model, size_t maxLen) {
    strncpy(brand, _activeVehicle.brand, maxLen);
    strncpy(model, _activeVehicle.model, maxLen);
}

// ======================== دریافت پروفایل خودرو ========================

bool VehicleDB::getVehicleProfile(uint8_t index, VehicleProfile& profile) {
    if (index < _vehicleCount) {
        profile = _vehicleList[index];
        return true;
    }
    return false;
}

// ======================== تعداد خودروها ========================

uint8_t VehicleDB::getVehicleCount() {
    return _vehicleCount;
}
