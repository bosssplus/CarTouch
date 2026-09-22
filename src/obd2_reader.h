/**
 * obd2_reader.h - خواندن داده‌های OBD-II از خودرو
 * 
 * این ماژول با ارسال درخواست‌های استاندارد OBD-II (SAE J1979)
 * به ECU خودرو، اطلاعاتی مثل RPM، سرعت، دما و ... را می‌خواند.
 * 
 * پروتکل: ISO 15765-4 (CAN 11-bit, 500Kbps)
 *
 * === بازطراحی non-blocking (چک‌لیست تجاری #4) ===
 * نسخه‌ی قبلی هر PID را با یک حلقه‌ی `while(millis() < timeout)` و
 * `delay()` بین PIDها می‌خواند؛ اگر ECU پاسخ نمی‌داد، این یعنی
 * blocking تا ۲۰۰ میلی‌ثانیه در هر تک PID و می‌توانست loop() اصلی
 * (پس زمینه‌ی LVGL/WebSocket) را قفل کند. این نسخه با یک state
 * machine داخلی جایگزین شده: readAllPIDs() دیگر مستقیماً منتظر
 * پاسخ نمی‌ماند، بلکه هر بار که از loop() صدا زده می‌شود («ادامه
 * بده اگر وقتشه») یک قدم کوچک جلو می‌رود. توابع قدیمی تک‌PID
 * (readEngineRPM و…) به‌عنوان API سازگار با نسخه‌ی قبل نگه داشته
 * شده‌اند ولی اکنون به‌صراحت [BLOCKING] علامت خورده‌اند تا مصرف‌کننده
 * بداند چه زمانی امن است ازشان استفاده کند (مثلاً یک تست دستی از
 * طریق Serial، نه در مسیر اصلی loop()).
 */

#ifndef OBD2_READER_H
#define OBD2_READER_H

#include <Arduino.h>
#include "config.h"
#include "can_manager.h"

// وضعیت‌های ماشین‌حالت خواندن غیرمسدودکننده‌ی چند-PID
enum ObdPollState : uint8_t {
    OBD_POLL_IDLE = 0,       // بین دو دور خواندن؛ منتظر شروع دور بعدی
    OBD_POLL_SENDING,        // در حال ارسال درخواست PID جاری
    OBD_POLL_WAITING,        // منتظر پاسخ PID جاری (بدون delay/بدون حلقه)
    OBD_POLL_DONE            // یک دور کامل (همه PIDها) تمام شد
};

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
     * [BLOCKING] ارسال درخواست OBD و دریافت پاسخ (PID تکی)
     * حداکثر ~۲۰۰ میلی‌ثانیه مسدود می‌کند. در loop() اصلی صدا نزنید؛
     * برای دیباگ دستی یا فراخوانی خارج از حلقه‌ی اصلی مناسب است.
     * برای خواندن دوره‌ای در پس‌زمینه از update()/getLatestData()
     * استفاده کنید.
     * @param pid شناسه PID (مثلاً 0x0C برای RPM)
     * @param response [out] ساختار پاسخ
     * @return true در صورت دریافت پاسخ معتبر
     */
    bool requestPID(uint8_t pid, ObdResponse& response);
    
    /**
     * [BLOCKING] خواندن دور موتور (RPM) - نگاه کنید به یادداشت requestPID
     * @return RPM یا 0 در صورت خطا
     */
    uint16_t readEngineRPM();
    
    /**
     * [BLOCKING] خواندن سرعت خودرو (km/h) - نگاه کنید به یادداشت requestPID
     * @return سرعت یا 0 در صورت خطا
     */
    uint8_t readVehicleSpeed();
    
    /**
     * [BLOCKING] خواندن دمای مایع خنک‌کننده - نگاه کنید به یادداشت requestPID
     * @return دما به درجه سانتی‌گراد یا -40 در صورت خطا
     */
    int8_t readCoolantTemp();
    
    /**
     * [BLOCKING] خواندن موقعیت دریچه گاز - نگاه کنید به یادداشت requestPID
     * @return درصد (0-100) یا 0 در صورت خطا
     */
    uint8_t readThrottlePosition();
    
    /**
     * [BLOCKING] خواندن سطح سوخت - نگاه کنید به یادداشت requestPID
     * @return درصد (0-100) یا 0 در صورت خطا
     */
    uint8_t readFuelLevel();
    
    /**
     * [BLOCKING] خواندن زمان روشن بودن موتور - نگاه کنید به یادداشت requestPID
     * @return زمان بر حسب ثانیه
     */
    uint16_t readEngineRuntime();
    
    /**
     * [BLOCKING - نگه‌داشته‌شده فقط برای سازگاری عقب‌رو] خواندن همه
     * PIDهای پایه به‌صورت مسدودکننده (مجموع تا ~۱٫۲ ثانیه در بدترین
     * حالت). دیگر در main.cpp/loop() صدا زده نمی‌شود - از update()
     * استفاده کنید. این تابع فقط برای کدی نگه داشته شده که ممکن است
     * جای دیگر (مثلاً یک اسکریپت تست) هنوز به آن وابسته باشد.
     * @param data [out] ساختار داده خودرو
     */
    void readAllPIDs(VehicleData& data);

    /**
     * [NON-BLOCKING] باید هر تکرار loop() صدا زده شود (شبیه
     * tftUI.update()). به‌صورت خودکار، با رعایت _requestInterval،
     * یک قدم از ماشین‌حالت خواندن دوره‌ای PIDها را جلو می‌برد: یک
     * درخواست می‌فرستد یا صف دریافت را (بدون مسدود کردن) چک می‌کند.
     * هرگز delay() یا حلقه‌ی انتظار ندارد.
     */
    void update();

    /**
     * آخرین داده‌ی کامل‌شده توسط update() را برمی‌گرداند. اگر هنوز
     * حتی یک دور کامل نشده، مقادیر پیش‌فرض VehicleData (صفر/UNKNOWN)
     * را می‌دهد.
     * @return true اگر حداقل یک دور کامل انجام شده باشد
     */
    bool getLatestData(VehicleData& outData);

    /**
     * وضعیت جاری ماشین‌حالت update() را برمی‌گرداند (برای نمایش
     * "در حال خواندن..." یا تشخیص timeout مکرر در UI/دیباگ)
     */
    ObdPollState getPollState();
    
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

    // ==================== state machine غیرمسدودکننده (update()) ====================
    // ترتیب PIDهایی که هر دور خوانده می‌شوند
    static const uint8_t _POLL_PID_COUNT = 6;
    ObdPollState _pollState;
    uint8_t _pollIndex;              // اندیس PID جاری در _POLL_PID_COUNT
    uint32_t _pollWaitStartMs;       // زمان شروع انتظار پاسخ PID جاری
    VehicleData _pendingData;        // داده در حال ساخته‌شدن طی دور جاری
    VehicleData _latestData;         // آخرین دور کامل‌شده
    bool _hasCompletedRound;
    uint32_t _pollIntervalMs;        // فاصله بین دو دور کامل (نه بین PIDها)
    uint32_t _lastRoundStartMs;

    void _pollStartNextPid();        // ارسال درخواست PID جاری (non-blocking)
    void _pollCheckResponse();       // چک غیرمسدودکننده‌ی صف دریافت
    void _applyPidToData(uint8_t pid, const ObdResponse& resp, VehicleData& data);
};

#endif // OBD2_READER_H
