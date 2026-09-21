/**
 * custom_vehicle.h - ساختارهای داده برای پروفایل‌های خودروی سفارشی
 * 
 * این فایل بخشی از CarTouch v2.0 است (به CarTouch_V2_SPEC.md مراجعه کنید).
 * 
 * دو منبع فرمان جدید (علاوه بر DBC موجود در vehicle_db.h) را تعریف می‌کند:
 *   1. Learned  - فرمانی که از دیدن CAN Bus واقعی هنگام فشردن دکمه
 *                 فیزیکی خودرو به‌دست آمده (به learn_engine.h مراجعه کنید)
 *   2. Manual   - فرمانی که کاربر مستقیماً CAN ID و بایت‌ها را وارد کرده
 * 
 * هر دو در یک ساختار مشترک LearnedCommand ذخیره می‌شوند چون از نظر
 * دستگاه یکسان‌اند: یک CAN ID + بایت‌های ثابت که باید تا زمان تأیید
 * صریح کاربر (status == VERIFIED) قابل اجرا نباشند.
 * 
 * ⚠️ نکته ایمنی مهم: وجود یک LearnedCommand در این ساختار به‌معنای
 * "مجاز به اجرا" نیست. فقط زمانی executeCommand() در vehicle_control
 * اجازه‌ی ارسال واقعی می‌دهد که status == CMD_VERIFIED باشد.
 */

#ifndef CUSTOM_VEHICLE_H
#define CUSTOM_VEHICLE_H

#include <Arduino.h>
#include "config.h"

// ======================== محدودیت‌های حافظه ========================
// طبق بخش ۴.۳ سند مشخصات - محاسبه شده تا از باگ قبلی نوع
// MAX_DBC_SIGNALS=200 (که ۱.۲ مگابایت RAM می‌شد) تکرار نشود.
// هر LearnedCommand ~۵۰ بایت -> ۳۲ فرمان * ۸ خودرو ~= ۱۲.۸ کیلوبایت کل.

#define MAX_CUSTOM_VEHICLES               8
#define MAX_LEARNED_COMMANDS_PER_VEHICLE  32

// ======================== وضعیت تأیید فرمان ========================

enum CommandStatus : uint8_t {
    CMD_UNVERIFIED = 0,  // ذخیره شده ولی هنوز کاربر تأیید نکرده - قابل اجرا نیست
    CMD_VERIFIED   = 1   // کاربر صریحاً تأیید کرده که روی خودرو کار می‌کند
};

// ======================== منبع فرمان ========================

enum CommandSource : uint8_t {
    SOURCE_DBC     = 0,  // از فایل DBC (vehicle_db) - سیگنال نوشتن (در عمل نادر)
    SOURCE_LEARNED = 1,  // از حالت یادگیری (learn_engine) با دکمه فیزیکی واقعی
    SOURCE_MANUAL  = 2   // وارد شده دستی توسط کاربر
};

// برچسب‌های استاندارد پیشنهادی برای فرمان‌ها (بخش ۴.۱ سند).
// این‌ها فقط رشته‌های پیشنهادی‌اند؛ کاربر می‌تواند برچسب دلخواه
// (custom) هم وارد کند، بنابراین به‌صورت enum سخت‌گیرانه نیستند،
// بلکه به‌عنوان ثابت‌های رشته‌ای در custom_vehicle_store.cpp تعریف
// می‌شوند تا هم UI (TFT/وب) و هم executeCommand() از همان رشته‌ها
// استفاده کنند.
#define CMD_LABEL_LOCK_ALL         "lock_all"
#define CMD_LABEL_UNLOCK_ALL       "unlock_all"
#define CMD_LABEL_UNLOCK_DRIVER    "unlock_driver"
#define CMD_LABEL_WINDOW_FL_UP     "window_fl_up"
#define CMD_LABEL_WINDOW_FL_DOWN   "window_fl_down"
#define CMD_LABEL_WINDOW_FR_UP     "window_fr_up"
#define CMD_LABEL_WINDOW_FR_DOWN   "window_fr_down"
#define CMD_LABEL_WINDOW_RL_UP     "window_rl_up"
#define CMD_LABEL_WINDOW_RL_DOWN   "window_rl_down"
#define CMD_LABEL_WINDOW_RR_UP     "window_rr_up"
#define CMD_LABEL_WINDOW_RR_DOWN   "window_rr_down"
#define CMD_LABEL_ALL_WINDOWS_UP   "all_windows_up"
#define CMD_LABEL_ALL_WINDOWS_DOWN "all_windows_down"
#define CMD_LABEL_SUNROOF_OPEN     "sunroof_open"
#define CMD_LABEL_SUNROOF_CLOSE    "sunroof_close"
#define CMD_LABEL_SUNROOF_TILT     "sunroof_tilt"
#define CMD_LABEL_TRUNK_OPEN       "trunk_open"
#define CMD_LABEL_TRUNK_LOCK       "trunk_lock"
#define CMD_LABEL_MIRROR_FOLD      "mirror_fold"
#define CMD_LABEL_MIRROR_UNFOLD    "mirror_unfold"
#define CMD_LABEL_ALARM_ARM        "alarm_arm"
#define CMD_LABEL_ALARM_DISARM     "alarm_disarm"
// برای برچسب دلخواه (custom)، کاربر یک رشته‌ی آزاد وارد می‌کند که
// مستقیماً به‌عنوان label ذخیره می‌شود (بدون پیشوند خاص).

// ======================== ساختار یک فرمان یادگرفته‌شده/دستی ========================

struct LearnedCommand {
    char label[32] = {0};          // شناسه‌ی داخلی (مثلاً "lock_all" یا نام دلخواه)
    char displayName[48] = {0};    // نام نمایشی فارسی (مثلاً "قفل همه درب‌ها")
    
    uint32_t canId = 0;
    bool isExtended = false;
    uint8_t length = 0;
    uint8_t data[8] = {0};
    
    CommandSource source = SOURCE_MANUAL;
    CommandStatus status = CMD_UNVERIFIED;
    
    uint8_t timesObserved = 0;     // چند بار در capture دیده شد (فقط برای Learned)
    uint8_t failCount = 0;         // چند بار تأیید ناموفق بود
    uint32_t createdAt = 0;        // millis() زمان ایجاد (برای نمایش/دیباگ - بعد از ریبوت بی‌معنی می‌شود، فقط مرجع داخلی جلسه)
};

// ======================== ساختار یک پروفایل خودروی سفارشی ========================

struct CustomVehicleProfile {
    uint8_t id = 0;                 // شاخص در فایل ایندکس (0..MAX_CUSTOM_VEHICLES-1)
    char name[32] = {0};            // نام دلخواه کاربر: "پراید بابا"
    char brand[24] = {0};
    char model[24] = {0};
    uint16_t year = 0;
    
    LearnedCommand commands[MAX_LEARNED_COMMANDS_PER_VEHICLE];
    uint8_t commandCount = 0;
    
    bool inUse = false;             // اگر false یعنی این اسلات خالی است
};

#endif // CUSTOM_VEHICLE_H
