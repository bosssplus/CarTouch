/**
 * active_profile_manager.h - لایه‌ی یکپارچه‌ساز منابع فرمان
 * 
 * بخشی از CarTouch v2.0 (به CarTouch_V2_SPEC.md بخش ۳.۱ و ۷.۱ مراجعه کنید).
 * 
 * این کلاس واسط بین دو منبع فرمان است:
 *   - VehicleDB (فایل‌های DBC آماده - قابلیت موجود v1.0، بدون تغییر)
 *   - CustomVehicleStore (پروفایل‌های Learned/Manual - جدید در v2.0)
 * 
 * vehicle_control.cpp دیگر مستقیماً با VehicleDB یا CustomVehicleStore
 * کار نمی‌کند؛ فقط از این کلاس می‌پرسد "برای خودروی فعلی، فرمان با
 * این برچسب چیست؟" و یک CanMessage آماده‌ی ارسال (یا خطا) می‌گیرد.
 * 
 * ⚠️ نکته ایمنی: این کلاس مسئول چک کردن CMD_VERIFIED است. اگر منبع
 * فعال یک پروفایل سفارشی (Learned/Manual) باشد و فرمان مربوطه هنوز
 * CMD_UNVERIFIED باشد، resolveCommand باید false برگرداند - صرف
 * نظر از اینکه صدا زننده (vehicle_control) خودش این چک را دوباره
 * انجام می‌دهد یا نه (دفاع لایه‌ای / defense in depth).
 */

#ifndef ACTIVE_PROFILE_MANAGER_H
#define ACTIVE_PROFILE_MANAGER_H

#include <Arduino.h>
#include "vehicle_db.h"
#include "custom_vehicle_store.h"
#include "can_manager.h"

// نوع منبع خودروی فعال در حال حاضر
enum ActiveVehicleKind : uint8_t {
    ACTIVE_KIND_NONE   = 0,  // هنوز هیچ خودرویی انتخاب نشده
    ACTIVE_KIND_DBC    = 1,  // یک پروفایل DBC آماده فعال است
    ACTIVE_KIND_CUSTOM = 2   // یک پروفایل سفارشی (Learned/Manual) فعال است
};

class ActiveProfileManager {
public:
    ActiveProfileManager(VehicleDB& vehicleDB, CustomVehicleStore& customStore);
    
    /**
     * انتخاب یک خودروی DBC آماده به‌عنوان فعال (مسیر قدیمی موجود،
     * دست‌نخورده - صرفاً wrapper دور VehicleDB::setActiveVehicle)
     */
    void selectDBCVehicle(const char* brand, const char* model);
    
    /**
     * انتخاب یک پروفایل سفارشی (Learned/Manual) به‌عنوان فعال
     * @param profileIndex ایندکس در CustomVehicleStore
     * @return true اگر پروفایل معتبر بود
     */
    bool selectCustomVehicle(uint8_t profileIndex);
    
    /**
     * نوع خودروی فعال فعلی
     */
    ActiveVehicleKind getActiveKind();
    
    /**
     * حل کردن یک برچسب فرمان (مثلاً "lock_all") به یک CanMessage
     * قابل ارسال، بر اساس منبع فعال فعلی.
     * 
     * @param label برچسب فرمان (از ثابت‌های CMD_LABEL_* در custom_vehicle.h)
     * @param outMsg [out] پیام آماده ارسال در صورت موفقیت
     * @param outErrorReason [out] در صورت شکست، دلیل به فارسی (برای نمایش در UI)
     * @return true اگر فرمان پیدا و مجاز به اجرا بود (یعنی یا از
     *         DBC است، یا از پروفایل سفارشی با status==CMD_VERIFIED)
     */
    bool resolveCommand(const char* label, CanMessage& outMsg, String& outErrorReason);
    
    /**
     * نام نمایشی خودروی فعال فعلی (برای نمایش در UI)
     */
    void getActiveVehicleName(char* outBuf, size_t maxLen);
    
    /**
     * ایندکس پروفایل سفارشی فعال (فقط معتبر اگر getActiveKind() == ACTIVE_KIND_CUSTOM)
     */
    uint8_t getActiveCustomIndex();

private:
    VehicleDB& _vehicleDB;
    CustomVehicleStore& _customStore;
    
    ActiveVehicleKind _activeKind;
    uint8_t _activeCustomIndex;
};

#endif // ACTIVE_PROFILE_MANAGER_H
