/**
 * vehicle_control.h - کنترل اجزای خودرو از طریق CAN Bus
 * 
 * این ماژول وظیفه ارسال دستورات کنترلی به خودرو را بر عهده دارد.
 * دستورات بر اساس اطلاعات فایل‌های DBC و استانداردهای خودروسازان ارسال می‌شوند.
 * 
 * تذکر: دستورات دقیق به مدل خودرو بستگی دارد و ممکن است نیاز به تنظیم
 * مقادیر CAN ID و داده‌ها بر اساس DBC مخصوص خودروی شما داشته باشد.
 * 
 * تمام توابع این فایل تست شده (روی شبیه‌ساز CAN) ولی مقادیر پیش‌فرض
 * DBC ممکن است برای خودروی شما نیاز به تنظیم داشته باشد.
 */

#ifndef VEHICLE_CONTROL_H
#define VEHICLE_CONTROL_H

#include <Arduino.h>
#include "config.h"
#include "can_manager.h"

/**
 * کلاس کنترل خودرو
 * 
 * ارسال دستورات CAN برای: قفل/بازکردن درب‌ها، شیشه‌ها، سانروف، صندوق عقب، آینه‌ها و دزدگیر
 */
class VehicleControl {
public:
    /**
     * سازنده
     * @param canManager reference به CANManager
     */
    VehicleControl(CANManager& canManager);
    
    /**
     * مقداردهی اولیه
     */
    void begin();
    
    // ======================== درب‌ها ========================
    
    /**
     * قفل کردن همه درب‌ها
     * @return true در صورت موفقیت
     */
    bool lockAllDoors();
    
    /**
     * باز کردن قفل همه درب‌ها
     * @return true در صورت موفقیت
     */
    bool unlockAllDoors();
    
    /**
     * باز کردن قفل درب راننده
     * @return true در صورت موفقیت
     */
    bool unlockDriverDoor();
    
    // ======================== شیشه‌ها ========================
    
    /**
     * بالاآوردن شیشه
     * @param window شناسه شیشه (0=FL, 1=FR, 2=RL, 3=RR)
     * @return true در صورت موفقیت
     */
    bool windowUp(uint8_t window);
    
    /**
     * پایین‌آوردن شیشه
     * @param window شناسه شیشه (0=FL, 1=FR, 2=RL, 3=RR)
     * @return true در صورت موفقیت
     */
    bool windowDown(uint8_t window);
    
    /**
     * بالاآوردن همه شیشه‌ها
     * @return true در صورت موفقیت
     */
    bool allWindowsUp();
    
    /**
     * پایین‌آوردن همه شیشه‌ها
     * @return true در صورت موفقیت
     */
    bool allWindowsDown();
    
    // ======================== سانروف ========================
    
    /**
     * باز کردن سانروف
     * @return true در صورت موفقیت
     */
    bool sunroofOpen();
    
    /**
     * بستن سانروف
     * @return true در صورت موفقیت
     */
    bool sunroofClose();
    
    /**
     * کج کردن سانروف (Tilt)
     * @return true در صورت موفقیت
     */
    bool sunroofTilt();
    
    // ======================== صندوق عقب ========================
    
    /**
     * باز کردن صندوق عقب
     * @return true در صورت موفقیت
     */
    bool trunkOpen();
    
    /**
     * قفل کردن صندوق عقب
     * @return true در صورت موفقیت
     */
    bool trunkLock();
    
    // ======================== آینه‌ها ========================
    
    /**
     * تا کردن آینه‌ها
     * @return true در صورت موفقیت
     */
    bool foldMirrors();
    
    /**
     * باز کردن آینه‌ها
     * @return true در صورت موفقیت
     */
    bool unfoldMirrors();
    
    // ======================== دزدگیر ========================
    
    /**
     * فعال کردن دزدگیر
     * @return true در صورت موفقیت
     */
    bool alarmArm();
    
    /**
     * غیرفعال کردن دزدگیر
     * @return true در صورت موفقیت
     */
    bool alarmDisarm();
    
    // ======================== عمومی ========================
    
    /**
     * توقف همه عملیات در حال اجرا
     * (مثلاً توقف حرکت شیشه‌ها)
     * @return true در صورت موفقیت
     */
    bool stopAll();
    
    /**
     * تنظیم CAN IDهای سفارشی برای مدل خودرو
     * @param doorLock CAN ID قفل درب
     * @param window CAN ID شیشه‌ها
     * @param sunroof CAN ID سانروف
     * @param trunk CAN ID صندوق عقب
     * @param mirror CAN ID آینه
     * @param alarm CAN ID دزدگیر
     */
    void setCustomCANIDs(uint32_t doorLock, uint32_t window, uint32_t sunroof,
                         uint32_t trunk, uint32_t mirror, uint32_t alarm);
    
    /**
     * دریافت آخرین خطا
     */
    uint8_t getLastError();

private:
    CANManager& _can;
    uint8_t _lastError;
    
    // CAN IDهای پیش‌فرض (مقادیر رایج در خودروهای OBD-II)
    // توجه: این مقادیر بر اساس رایج‌ترین خودروها حدس زده شده‌اند.
    // برای خودروی خاص خود، از فایل DBC مخصوص استفاده کنید.
    uint32_t _canIdDoorLock;    // پیش‌فرض: 0x1A0
    uint32_t _canIdWindow;      // پیش‌فرض: 0x1A1
    uint32_t _canIdSunroof;     // پیش‌فرض: 0x1A2
    uint32_t _canIdTrunk;       // پیش‌فرض: 0x1A3
    uint32_t _canIdMirror;      // پیش‌فرض: 0x1A4
    uint32_t _canIdAlarm;       // پیش‌فرض: 0x1A5
    
    // تابع داخلی برای ساخت و ارسال پیام CAN
    bool _sendCommand(uint32_t canId, const uint8_t* data, uint8_t length);
};

#endif // VEHICLE_CONTROL_H
