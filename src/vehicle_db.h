/**
 * vehicle_db.h - پایگاه داده خودروها (DBC Parser)
 * 
 * این ماژول وظیفه بارگذاری و تفسیر فایل‌های DBC (CAN Database) را دارد.
 * از دیتابیس OpenDBC از comma.ai استفاده می‌کند که بیش از 300 مدل خودرو را پشتیبانی می‌کند.
 * 
 * فایل‌های DBC شامل:
 *   - شناسه‌های CAN برای هر فرمان
 *   - فرمت داده‌ها (بایت‌ها، بیت‌ها، ضریب‌ها)
 *   - توضیحات هر سیگنال
 * 
 * توجه: این ماژول از یک پارسر DBC ساده استفاده می‌کند.
 * فایل‌های DBC پیچیده با multiplexing ممکن است کامل پشتیبانی نشوند.
 * تمام توابع این فایل تست شده ولی پارسر کامل DBC نیاز به توسعه بیشتر دارد.
 */

#ifndef VEHICLE_DB_H
#define VEHICLE_DB_H

#include <Arduino.h>
#include <vector>
#include "config.h"

// حداکثر تعداد سیگنال‌ها و پیام‌ها
#define MAX_DBC_MESSAGES    50
#define MAX_DBC_SIGNALS     200

// نوع داده سیگنال DBC
enum SignalType : uint8_t {
    SIG_UNSIGNED = 0,
    SIG_SIGNED   = 1,
    SIG_FLOAT    = 2,
    SIG_UNKNOWN  = 3
};

// ساختار یک سیگنال DBC
struct DbcSignal {
    char name[32] = {0};          // نام سیگنال (مثلاً "DoorLockStatus")
    uint8_t startBit = 0;         // بیت شروع
    uint8_t length = 0;           // طول سیگنال (بیت)
    SignalType type = SIG_UNSIGNED;
    float scale = 1.0f;           // ضریب
    float offset = 0.0f;          // مقدار آفست
    float min = 0.0f;             // حداقل مقدار
    float max = 100.0f;           // حداکثر مقدار
    char unit[8] = {0};           // واحد (مثلاً "km/h")
    char comment[64] = {0};       // توضیحات
    bool isMultiplexed = false;   // اگر true: سیگنال Multiplexed
    uint8_t multiplexValue = 0;   // مقدار Multiplex
};

// ساختار یک پیام DBC
struct DbcMessage {
    uint32_t canId = 0;           // CAN ID
    char name[32] = {0};          // نام پیام (مثلاً "DoorStatus")
    uint8_t dlc = 8;              // طول داده
    char transmitter[24] = {0};   // فرستنده
    uint8_t signalCount = 0;      // تعداد سیگنال‌ها
    DbcSignal signals[MAX_DBC_SIGNALS];
};

// ساختار یک خودرو
struct VehicleProfile {
    char brand[24] = {0};         // برند (مثلاً "Toyota")
    char model[24] = {0};         // مدل (مثلاً "Camry")
    uint16_t yearStart = 0;       // سال شروع
    uint16_t yearEnd = 0;         // سال پایان
    char dbcFileName[32] = {0};   // نام فایل DBC
};

/**
 * کلاس پایگاه داده خودرو
 */
class VehicleDB {
public:
    VehicleDB();
    
    /**
     * مقداردهی اولیه و بارگذاری لیست خودروها
     */
    void begin();
    
    /**
     * بارگذاری فایل DBC از SPIFFS
     * @param filename نام فایل (مثلاً "/dbc/toyota_camry_2020.dbc")
     * @return true در صورت موفقیت
     */
    bool loadDBCFile(const char* filename);
    
    /**
     * پیدا کردن پیام در DBC با CAN ID
     * @param canId شناسه CAN
     * @return pointer به پیام یا NULL
     */
    DbcMessage* findMessageByID(uint32_t canId);
    
    /**
     * پیدا کردن سیگنال در یک پیام
     * @param msg پیام DBC
     * @param signalName نام سیگنال
     * @return pointer به سیگنال یا NULL
     */
    DbcSignal* findSignal(DbcMessage* msg, const char* signalName);
    
    /**
     * استخراج مقدار یک سیگنال از داده‌های خام CAN
     * @param signal سیگنال DBC
     * @param data داده‌های خام (8 بایت)
     * @return مقدار استخراج شده
     */
    float extractSignalValue(const DbcSignal& signal, const uint8_t* data);
    
    /**
     * تبدیل مقدار به داده‌های خام CAN برای ارسال
     * @param signal سیگنال DBC
     * @param value مقدار مورد نظر
     * @param data [out] داده‌های خام (8 بایت)
     */
    void encodeSignalValue(const DbcSignal& signal, float value, uint8_t* data);
    
    /**
     * دریافت تعداد پیام‌های بارگذاری شده
     */
    uint8_t getMessageCount();
    
    /**
     * دریافت pointer به پیام با ایندکس
     */
    DbcMessage* getMessageByIndex(uint8_t index);
    
    /**
     * تنظیم خودروی فعال
     * @param brand برند
     * @param model مدل
     */
    void setActiveVehicle(const char* brand, const char* model);
    
    /**
     * دریافت نام خودروی فعال
     */
    void getActiveVehicle(char* brand, char* model, size_t maxLen);
    
    /**
     * دریافت لیست خودروهای پشتیبانی‌شده
     * @param index ایندکس
     * @param profile [out] پروفایل خودرو
     * @return true در صورت وجود
     */
    bool getVehicleProfile(uint8_t index, VehicleProfile& profile);
    
    /**
     * دریافت تعداد خودروهای موجود
     */
    uint8_t getVehicleCount();

private:
    DbcMessage _messages[MAX_DBC_MESSAGES];
    uint8_t _messageCount;
    VehicleProfile _activeVehicle;
    VehicleProfile _vehicleList[10];  // لیست خودروهای پشتیبانی‌شده
    uint8_t _vehicleCount;
    bool _initialized;
    
    // پارس کردن خط DBC
    bool _parseMessageLine(const char* line);
    bool _parseSignalLine(const char* line);
    bool _parseValueLine(const char* line);
    bool _parseCommentLine(const char* line);
};

#endif // VEHICLE_DB_H
