/**
 * custom_vehicle_store.h - ذخیره‌سازی پایدار پروفایل‌های خودروی سفارشی
 * 
 * بخشی از CarTouch v2.0 (به CarTouch_V2_SPEC.md بخش ۶ مراجعه کنید).
 * 
 * پروفایل‌های سفارشی (Learned/Manual) بر خلاف AppConfig که یک ساختار
 * ثابت در NVS است، در فایل‌های JSON جداگانه در SPIFFS ذخیره می‌شوند
 * چون تعدادشان و اندازه‌ی فرمان‌هایشان متغیر است:
 * 
 *   /custom_vehicles/index.json          - لیست خلاصه (برای نمایش سریع)
 *   /custom_vehicles/profile_0.json      - جزئیات کامل پروفایل ۰
 *   /custom_vehicles/profile_1.json      - جزئیات کامل پروفایل ۱
 *   ...
 * 
 * تمام عملیات این فایل از ArduinoJson استفاده می‌کند که پروژه از قبل
 * به‌عنوان dependency دارد (bblanchon/ArduinoJson در platformio.ini) -
 * کتابخانه‌ی جدیدی اضافه نشده است.
 */

#ifndef CUSTOM_VEHICLE_STORE_H
#define CUSTOM_VEHICLE_STORE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "custom_vehicle.h"

class CustomVehicleStore {
public:
    CustomVehicleStore();
    
    /**
     * مقداردهی اولیه - پوشه /custom_vehicles را در صورت نبود می‌سازد
     * و ایندکس را از SPIFFS می‌خواند. باید بعد از SPIFFS.begin() و
     * قبل از استفاده از سایر متدها صدا زده شود.
     */
    bool begin();
    
    /**
     * تعداد پروفایل‌های سفارشی موجود (اسلات‌های inUse=true)
     */
    uint8_t getProfileCount();
    
    /**
     * خواندن خلاصه یک پروفایل با ایندکس (بدون فرمان‌های کامل - سریع)
     * برای نمایش لیست استفاده می‌شود.
     * @return true اگر اسلات پر باشد
     */
    bool getProfileSummary(uint8_t index, CustomVehicleProfile& outSummary);
    
    /**
     * خواندن کامل یک پروفایل (شامل تمام فرمان‌ها) از فایل profile_N.json
     * @return true در صورت موفقیت
     */
    bool loadProfile(uint8_t index, CustomVehicleProfile& outProfile);
    
    /**
     * ذخیره کامل یک پروفایل (ایجاد یا بازنویسی). ایندکس هم به‌روزرسانی
     * می‌شود.
     * @return true در صورت موفقیت
     */
    bool saveProfile(const CustomVehicleProfile& profile);
    
    /**
     * ساخت یک پروفایل جدید خالی و رزرو یک اسلات آزاد.
     * @param name نام دلخواه کاربر
     * @param outIndex [out] ایندکس اسلات رزروشده
     * @return true اگر جا بود
     */
    bool createNewProfile(const char* name, const char* brand, 
                          const char* model, uint16_t year, uint8_t& outIndex);
    
    /**
     * حذف کامل یک پروفایل (فایل + ایندکس)
     */
    bool deleteProfile(uint8_t index);
    
    /**
     * افزودن یا به‌روزرسانی یک فرمان در یک پروفایل موجود
     * (اگر label از قبل بود، بازنویسی می‌شود - برای یادگیری مجدد)
     * @return true در صورت موفقیت (جا کافی بود)
     */
    bool upsertCommand(uint8_t profileIndex, const LearnedCommand& cmd);
    
    /**
     * تغییر وضعیت یک فرمان (برای تأیید/رد بعد از verify_confirm)
     */
    bool setCommandStatus(uint8_t profileIndex, const char* label, 
                          CommandStatus newStatus, bool incrementFailCount = false);
    
    /**
     * پیدا کردن یک فرمان مشخص در یک پروفایل بر اساس label
     * @return true اگر پیدا شد
     */
    bool findCommand(uint8_t profileIndex, const char* label, LearnedCommand& outCmd);
    
    /**
     * صدور پروفایل به‌صورت رشته‌ی JSON خام (برای دانلود/export)
     */
    bool exportProfileJSON(uint8_t index, String& outJson);
    
    /**
     * وارد کردن یک پروفایل از رشته‌ی JSON (برای آپلود/import).
     * طبق سیاست امنیتی بخش ۶.۳: تمام فرمان‌های وارد‌شده صرف‌نظر از
     * status موجود در فایل ورودی، به CMD_UNVERIFIED تنزل داده
     * می‌شوند - چون این دستگاه با این فرمان‌ها روی این خودروی خاص
     * تست نشده است.
     * @param outIndex [out] ایندکس اسلات جدید
     * @return true در صورت موفقیت (JSON معتبر بود و جا وجود داشت)
     */
    bool importProfileJSON(const String& json, uint8_t& outIndex);

private:
    bool _initialized;
    
    // نگاشت ساده در RAM فقط برای خلاصه‌ها (نه فرمان‌های کامل) تا
    // لیست‌کردن سریع باشد بدون خواندن هر بار از فلش
    CustomVehicleProfile _summaryCache[MAX_CUSTOM_VEHICLES];
    
    String _profilePath(uint8_t index);
    bool _loadIndex();
    bool _saveIndex();
    bool _writeProfileFile(const CustomVehicleProfile& profile);
    bool _readProfileFile(uint8_t index, CustomVehicleProfile& outProfile);
    
    // تبدیل بین struct و JSON (استفاده داخلی)
    void _profileToJson(const CustomVehicleProfile& profile, JsonDocument& doc);
    bool _jsonToProfile(JsonDocument& doc, CustomVehicleProfile& profile);
};

#endif // CUSTOM_VEHICLE_STORE_H
