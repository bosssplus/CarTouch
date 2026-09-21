/**
 * vehicle_control.h - کنترل اجزای خودرو از طریق CAN Bus
 * 
 * === CarTouch v2.0 - بازطراحی با حفظ کامل API قدیم ===
 * طبق CarTouch_V2_SPEC.md بخش ۷.۱: این ماژول دیگر CAN ID های هاردکد
 * (0x1A0 و...) استفاده نمی‌کند. به‌جای آن از ActiveProfileManager
 * می‌خواهد که برچسب فرمان (مثلاً "lock_all") را بر اساس خودروی فعال
 * (DBC آماده یا پروفایل سفارشی Learned/Manual) به یک CanMessage
 * واقعی تبدیل کند.
 * 
 * تمام امضای توابع عمومی (lockAllDoors, unlockAllDoors, windowUp,
 * ...) دقیقاً مثل نسخه‌ی قبلی حفظ شده تا main.cpp و بقیه‌ی پروژه
 * بدون تغییر کار کنند. setCustomCANIDs() هم برای سازگاری عقب‌رو نگه
 * داشته شده (هرچند دیگر مسیر اصلی نیست).
 * 
 * ⚠️ اگر خودروی فعال یک پروفایل سفارشی باشد و فرمان مربوطه هنوز
 * CMD_UNVERIFIED باشد، اجرای فرمان با شکست مواجه می‌شود (false
 * برمی‌گردد) - این رفتار عمدی است، نه باگ. به بخش ۸ سند مراجعه کنید.
 */

#ifndef VEHICLE_CONTROL_H
#define VEHICLE_CONTROL_H

#include <Arduino.h>
#include "config.h"
#include "can_manager.h"
#include "active_profile_manager.h"

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
     * @param profileManager reference به ActiveProfileManager (جدید در v2.0)
     */
    VehicleControl(CANManager& canManager, ActiveProfileManager& profileManager);
    
    /**
     * مقداردهی اولیه
     */
    void begin();
    
    /**
     * === متد اصلی جدید v2.0 ===
     * اجرای یک فرمان با برچسب دلخواه (از ثابت‌های CMD_LABEL_* در
     * custom_vehicle.h، یا هر برچسبی که کاربر برای پروفایل سفارشی
     * تعریف کرده). این متد جایگزین مسیر داخلی تمام توابع زیر است.
     * @param commandLabel برچسب فرمان
     * @return true در صورت موفقیت ارسال
     */
    bool executeCommand(const char* commandLabel);
    
    /**
     * مثل executeCommand ولی دلیل شکست را هم برمی‌گرداند (برای نمایش
     * در UI - مثلاً "فرمان تأیید نشده است")
     */
    bool executeCommand(const char* commandLabel, String& outErrorReason);
    
    // ======================== درب‌ها (سازگاری با v1.0) ========================
    
    /** قفل کردن همه درب‌ها - داخلاً executeCommand(CMD_LABEL_LOCK_ALL) */
    bool lockAllDoors();
    
    /** باز کردن قفل همه درب‌ها */
    bool unlockAllDoors();
    
    /** باز کردن قفل درب راننده */
    bool unlockDriverDoor();
    
    // ======================== شیشه‌ها ========================
    
    /**
     * بالاآوردن شیشه
     * @param window شناسه شیشه (0=FL, 1=FR, 2=RL, 3=RR)
     */
    bool windowUp(uint8_t window);
    
    /**
     * پایین‌آوردن شیشه
     * @param window شناسه شیشه (0=FL, 1=FR, 2=RL, 3=RR)
     */
    bool windowDown(uint8_t window);
    
    bool allWindowsUp();
    bool allWindowsDown();
    
    // ======================== سانروف ========================
    
    bool sunroofOpen();
    bool sunroofClose();
    bool sunroofTilt();
    
    // ======================== صندوق عقب ========================
    
    bool trunkOpen();
    bool trunkLock();
    
    // ======================== آینه‌ها ========================
    
    bool foldMirrors();
    bool unfoldMirrors();
    
    // ======================== دزدگیر ========================
    
    bool alarmArm();
    bool alarmDisarm();
    
    // ======================== عمومی ========================
    
    /**
     * توقف همه عملیات در حال اجرا (مثلاً توقف حرکت شیشه‌ها)
     * توجه: این تابع در حالت DBC/سفارشی معنای مستقیم "توقف" ندارد
     * مگر برچسب‌های مخصوص برای آن یادگرفته/تعریف شده باشند؛ در غیر
     * این صورت یک پیام صفر (all-zero) به CAN ID های شناخته‌شده‌ی
     * legacy فعلی ارسال می‌شود - رفتار best-effort است، نه تضمینی.
     */
    bool stopAll();
    
    /**
     * (سازگاری عقب‌رو با v1.0 - دیگر مسیر اصلی نیست)
     * تنظیم دستی CAN IDهای ثابت بدون عبور از ActiveProfileManager.
     * اگر این متد صدا زده شود، حالت "override دستی legacy" فعال
     * می‌شود و فقط برای شش تابع پایه (قفل/شیشه/سانروف/صندوق/آینه/دزدگیر)
     * به‌جای ActiveProfileManager از این IDها استفاده می‌شود. توصیه
     * می‌شود به‌جای این تابع از «ورود دستی» در ActiveProfileManager/
     * CustomVehicleStore استفاده شود چون آن مسیر از چرخه‌ی
     * تأیید/UNVERIFIED ایمنی عبور می‌کند و این یکی نه.
     */
    void setCustomCANIDs(uint32_t doorLock, uint32_t window, uint32_t sunroof,
                         uint32_t trunk, uint32_t mirror, uint32_t alarm);
    
    /**
     * دریافت آخرین خطا
     */
    uint8_t getLastError();
    
    /**
     * دریافت آخرین پیام خطای متنی (فارسی) - برای نمایش در UI
     */
    String getLastErrorMessage();

private:
    CANManager& _can;
    ActiveProfileManager& _profileManager;
    uint8_t _lastError;
    String _lastErrorMessage;
    
    // اصلاحیه (حفظ‌شده از v1.0): rate-limit سطح پایین روی فرمان‌های
    // کنترلی. حتی اگر یک مسیر ورودی جدید (وب/TFT/API آینده)
    // بدون rate-limit بالاتر اضافه شود، این لایه از ارسال فرمان‌های
    // فیزیکی با سرعت خطرناک به موتور شیشه/قفل جلوگیری می‌کند.
    uint32_t _lastCommandTime;
    static const uint32_t MIN_COMMAND_INTERVAL_MS = 150;
    
    // === سازگاری عقب‌رو legacy (فقط اگر setCustomCANIDs صدا زده شده باشد) ===
    bool _legacyOverrideActive;
    uint32_t _canIdDoorLock;
    uint32_t _canIdWindow;
    uint32_t _canIdSunroof;
    uint32_t _canIdTrunk;
    uint32_t _canIdMirror;
    uint32_t _canIdAlarm;
    
    // تابع داخلی برای ارسال یک CanMessage از قبل حل‌شده (شامل rate-limit)
    bool _sendResolvedMessage(const CanMessage& msg);
    
    // مسیر legacy: ساخت و ارسال مستقیم با CAN ID/بایت ثابت (برای
    // سازگاری با setCustomCANIDs و توابع پایه‌ی قدیمی وقتی حالت
    // override legacy فعال است)
    bool _sendLegacyCommand(uint32_t canId, const uint8_t* data, uint8_t length);
    
    // اجرای یک برچسب: یا از مسیر legacy (اگر override فعال است) یا
    // از ActiveProfileManager
    bool _execute(const char* label, String& outErrorReason);
};

#endif // VEHICLE_CONTROL_H
