/**
 * obd2_reader.h - خواندن داده‌های OBD-II از خودرو
 * 
 * این ماژول با ارسال درخواست‌های استاندارد OBD-II (SAE J1979)
 * به ECU خودرو، اطلاعاتی مثل RPM، سرعت، دما و ... را می‌خواند.
 * 
 * پروتکل: ISO 15765-4 (CAN 11-bit, 500Kbps)
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#ifndef OBD2_READER_H
#define OBD2_READER_H

#include <Arduino.h>
#include "config.h"
#include "can_manager.h"

// ساختار پاسخ OBD-II
struct ObdResponse {
    uint8_t pid;            // PID درخواست شده
    uint8_t data[6];        // داده‌های پاسخ (حداکثر 6 بایت)
    uint8_t length;         // طول واقعی داده
    bool success;           // اگر true: پاسخ معتبر است
    uint32_t timestamp;     // timestamp دریافت (millis)
};

/**
 * کلاس خواندن داده‌های OBD-II
 * 
 * از CANManager برای ارسال/دریافت پیام‌های CAN استفاده می‌کند.
 */
class OBD2Reader {
public:
    /**
     * سازنده
     * @param canManager reference به CANManager
     */
    OBD2Reader(CANManager& canManager);
    
    /**
     * مقداردهی اولیه
     */
    void begin();
    
    /**
     * ارسال درخواست OBD و دریافت پاسخ (PID تکی)
     * @param pid شناسه PID (مثلاً 0x0C برای RPM)
     * @param response [out] ساختار پاسخ
     * @return true در صورت دریافت پاسخ معتبر
     */
    bool requestPID(uint8_t pid, ObdResponse& response);
    
    /**
     * خواندن دور موتور (RPM)
     * @return RPM یا 0 در صورت خطا
     */
    uint16_t readEngineRPM();
    
    /**
     * خواندن سرعت خودرو (km/h)
     * @return سرعت یا 0 در صورت خطا
     */
    uint8_t readVehicleSpeed();
    
    /**
     * خواندن دمای مایع خنک‌کننده
     * @return دما به درجه سانتی‌گراد یا -40 در صورت خطا
     */
    int8_t readCoolantTemp();
    
    /**
     * خواندن موقعیت دریچه گاز
     * @return درصد (0-100) یا 0 در صورت خطا
     */
    uint8_t readThrottlePosition();
    
    /**
     * خواندن سطح سوخت
     * @return درصد (0-100) یا 0 در صورت خطا
     */
    uint8_t readFuelLevel();
    
    /**
     * خواندن زمان روشن بودن موتور
     * @return زمان بر حسب ثانیه
     */
    uint16_t readEngineRuntime();
    
    /**
     * خواندن همه PIDهای پایه و پر کردن VehicleData
     * @param data [out] ساختار داده خودرو
     */
    void readAllPIDs(VehicleData& data);
    
    /**
     * بررسی پشتیبانی از یک PID (PID 0x00: PIDsSupported)
     * @param pid PID مورد نظر
     * @return true اگر ECU از این PID پشتیبانی کند
     */
    bool isPidSupported(uint8_t pid);
    
    /**
     * دریافت لیست کدهای خطا (DTC)
     * @param dtcList [out] آرایه کدهای خطا
     * @param maxCount حداکثر تعداد DTC
     * @return تعداد DTCهای یافت شده
     */
    uint8_t readDTCs(uint16_t dtcList[], uint8_t maxCount = MAX_DTC_COUNT);
    
    /**
     * پاک کردن کدهای خطا (Clear DTC)
     * @return true در صورت موفقیت
     */
    bool clearDTCs();
    
    /**
     * آخرین خطا را برمی‌گرداند
     */
    uint8_t getLastError();

private:
    CANManager& _can;
    uint8_t _lastError;
    uint32_t _lastRequestTime;
    uint32_t _requestInterval;  // حداقل فاصله بین درخواست‌ها (ms)
    
    // تابع داخلی برای ارسال درخواست و دریافت پاسخ
    bool _sendOBDRequest(uint8_t pid, uint8_t expectedDataLength);
    bool _parseOBDResponse(const uint8_t* rawData, uint8_t length, uint8_t pid, ObdResponse& response);
};

#endif // OBD2_READER_H
