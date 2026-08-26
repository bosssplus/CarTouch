/**
 * can_manager.h - مدیریت CAN Bus
 * 
 * این ماژول وظیفه ارتباط سطح پایین با CAN Bus را بر عهده دارد.
 * از کتابخانه توکار ESP32 (TWAI) برای ارسال و دریافت پیام‌های CAN استفاده می‌کند.
 * 
 * کتابخانه TWAI (Two-Wire Automotive Interface) بخشی از ESP-IDF است
 * و نیازی به نصب کتابخانه جداگانه ندارد.
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <Arduino.h>
#include "config.h"

// ساختار پیام CAN
struct CanMessage {
    uint32_t id;        // شناسه پیام (11 یا 29 بیتی)
    uint8_t data[8];    // داده (حداکثر 8 بایت)
    uint8_t length;     // طول واقعی داده (0-8)
    bool isExtended;    // اگر true: شناسه 29 بیتی (CAN 2.0B)
    bool isRemote;      // اگر true: قاب RTR (Remote Transmission Request)
};

// نوع خطاهای CAN
enum CanError : uint8_t {
    CAN_OK = 0,
    CAN_ERROR_INIT = 1,
    CAN_ERROR_TX = 2,
    CAN_ERROR_RX = 3,
    CAN_ERROR_BUS_OFF = 4,
    CAN_ERROR_TIMEOUT = 5
};

/**
 * کلاس مدیریت CAN Bus
 * 
 * wrapper ای بر روی TWAI driver برای ساده‌سازی عملیات
 */
class CANManager {
public:
    /**
     * سازنده کلاس
     * @param txPin پین TX
     * @param rxPin پین RX
     * @param speed نرخ انتقال (bps)
     */
    CANManager(uint8_t txPin = PIN_CAN_TX, 
               uint8_t rxPin = PIN_CAN_RX,
               uint32_t speed = CAN_SPEED);
    
    /**
     * مقداردهی اولیه CAN Bus
     * @return true در صورت موفقیت
     */
    bool begin();
    
    /**
     * توقف CAN Bus و آزادسازی منابع
     */
    void end();
    
    /**
     * ارسال یک پیام CAN
     * @param msg پیام CAN
     * @param timeout مهلت ارسال (میلی‌ثانیه)
     * @return true در صورت موفقیت
     */
    bool sendMessage(const CanMessage& msg, uint32_t timeout = CAN_LISTEN_TIMEOUT);
    
    /**
     * دریافت یک پیام CAN (مسدودکننده)
     * @param msg [out] پیام دریافتی
     * @param timeout مهلت دریافت (میلی‌ثانیه)
     * @return true در صورت دریافت پیام
     */
    bool receiveMessage(CanMessage& msg, uint32_t timeout = CAN_LISTEN_TIMEOUT);
    
    /**
     * دریافت یک پیام CAN (غیرمسدودکننده)
     * @param msg [out] پیام دریافتی
     * @return true در صورت دریافت پیام
     */
    bool receiveMessageNonBlocking(CanMessage& msg);
    
    /**
     * پاک کردن صف پیام‌های دریافتی
     */
    void flushRxQueue();
    
    /**
     * بررسی وضعیت CAN Bus
     * @return true اگر CAN فعال باشد
     */
    bool isActive();
    
    /**
     * بررسی وضعیت خطا
     * @return آخرین خطای رخ داده
     */
    CanError getLastError();
    
    /**
     * بازنشانی CAN driver بعد از خطای BUS_OFF
     * @return true در صورت موفقیت
     */
    bool recoverFromBusOff();
    
    /**
     * دریافت آمار CAN Bus
     * @param txCount [out] تعداد ارسال‌ها
     * @param rxCount [out] تعداد دریافت‌ها
     * @param errorCount [out] تعداد خطاها
     */
    void getStats(uint32_t& txCount, uint32_t& rxCount, uint32_t& errorCount);

private:
    uint8_t _txPin;
    uint8_t _rxPin;
    uint32_t _speed;
    bool _initialized;
    CanError _lastError;
    uint32_t _txCount;
    uint32_t _rxCount;
    uint32_t _errorCount;
};

#endif // CAN_MANAGER_H
