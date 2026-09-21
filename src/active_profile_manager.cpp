/**
 * active_profile_manager.cpp - پیاده‌سازی لایه‌ی یکپارچه‌ساز منابع فرمان
 */

#include "active_profile_manager.h"
#include "custom_vehicle.h"

// ======================== سازنده ========================

ActiveProfileManager::ActiveProfileManager(VehicleDB& vehicleDB, CustomVehicleStore& customStore)
    : _vehicleDB(vehicleDB), _customStore(customStore) {
    _activeKind = ACTIVE_KIND_NONE;
    _activeCustomIndex = 0;
}

// ======================== انتخاب خودروی DBC ========================

void ActiveProfileManager::selectDBCVehicle(const char* brand, const char* model) {
    _vehicleDB.setActiveVehicle(brand, model);
    _activeKind = ACTIVE_KIND_DBC;
    Serial.printf("[APM] خودروی فعال (DBC): %s %s\n", brand, model);
}

// ======================== انتخاب خودروی سفارشی ========================

bool ActiveProfileManager::selectCustomVehicle(uint8_t profileIndex) {
    CustomVehicleProfile summary;
    if (!_customStore.getProfileSummary(profileIndex, summary)) {
        Serial.printf("⚠️ [APM] پروفایل سفارشی %d یافت نشد\n", profileIndex);
        return false;
    }
    
    _activeCustomIndex = profileIndex;
    _activeKind = ACTIVE_KIND_CUSTOM;
    Serial.printf("[APM] خودروی فعال (سفارشی): %s\n", summary.name);
    return true;
}

// ======================== نوع فعال ========================

ActiveVehicleKind ActiveProfileManager::getActiveKind() {
    return _activeKind;
}

// ======================== ایندکس سفارشی فعال ========================

uint8_t ActiveProfileManager::getActiveCustomIndex() {
    return _activeCustomIndex;
}

// ======================== حل کردن فرمان ========================

bool ActiveProfileManager::resolveCommand(const char* label, CanMessage& outMsg, String& outErrorReason) {
    if (_activeKind == ACTIVE_KIND_NONE) {
        outErrorReason = "هیچ خودرویی انتخاب نشده است";
        return false;
    }
    
    if (_activeKind == ACTIVE_KIND_DBC) {
        // === مسیر DBC ===
        // توجه مهم (طبق بخش ۷.۲ سند): اکثر فایل‌های DBC موجود در این
        // پروژه (از OpenDBC) عمدتاً سیگنال‌های خواندن دارند، نه
        // فرمان‌های نوشتن برای قفل/شیشه. در پیاده‌سازی فعلی، جستجوی
        // فرمان‌های نوشتن در DBC هنوز به یک قرارداد نام‌گذاری خاص
        // متکی نیست (چون در عمل به‌ندرت این داده در فایل‌های DBC
        // موجود پیدا می‌شود). به همین دلیل مسیر DBC فعلاً یک پیام
        // خطای صادقانه برمی‌گرداند تا کاربر به‌جای شکست خاموش، دقیقاً
        // بداند چرا و به سمت Learn Mode / ورود دستی هدایت شود.
        //
        // اگر در آینده یک قرارداد نام‌گذاری سیگنال (مثلاً سیگنالی با
        // نام دقیقاً برابر با label، مثل "lock_all") در یک DBC خاص
        // پیدا و تأیید شد، این‌جا باید vehicleDB.findMessageByID /
        // findSignal برای آن سیگنال خاص فراخوانی و encodeSignalValue
        // استفاده شود. این کار عمداً در این نسخه پیاده‌سازی نشده چون
        // حدس زدن چنین قراردادی بدون داده‌ی واقعی خطرناک‌تر از
        // صادق بودن درباره‌ی نبودش است.
        outErrorReason = "این فایل DBC فرمان نوشتن برای این عملکرد ندارد - "
                         "از «حالت یادگیری» یا «ورود دستی» استفاده کنید";
        return false;
    }
    
    // === مسیر سفارشی (Learned / Manual) ===
    LearnedCommand cmd;
    if (!_customStore.findCommand(_activeCustomIndex, label, cmd)) {
        outErrorReason = "فرمانی با این برچسب برای این خودرو یادگرفته/ثبت نشده است";
        return false;
    }
    
    // ⚠️ چک حیاتی ایمنی - دفاع لایه‌ای. vehicle_control.cpp هم این
    // چک را دارد، ولی این‌جا هم تکرار می‌شود تا این کلاس به‌تنهایی
    // (مثلاً اگر در آینده جای دیگری هم صدا زده شد) هرگز یک فرمان
    // تأییدنشده را به‌عنوان قابل‌ارسال برنگرداند.
    if (cmd.status != CMD_VERIFIED) {
        outErrorReason = "این فرمان هنوز تأیید نشده (UNVERIFIED) - "
                         "ابتدا از منوی «فرمان‌های من» تأیید کنید";
        return false;
    }
    
    outMsg.id = cmd.canId;
    outMsg.isExtended = cmd.isExtended;
    outMsg.isRemote = false;
    outMsg.length = cmd.length;
    memcpy(outMsg.data, cmd.data, cmd.length);
    
    return true;
}

// ======================== نام خودروی فعال ========================

void ActiveProfileManager::getActiveVehicleName(char* outBuf, size_t maxLen) {
    if (_activeKind == ACTIVE_KIND_DBC) {
        char brand[24], model[24];
        _vehicleDB.getActiveVehicle(brand, model, sizeof(brand));
        snprintf(outBuf, maxLen, "%s %s", brand, model);
    } else if (_activeKind == ACTIVE_KIND_CUSTOM) {
        CustomVehicleProfile summary;
        if (_customStore.getProfileSummary(_activeCustomIndex, summary)) {
            strncpy(outBuf, summary.name, maxLen - 1);
            outBuf[maxLen - 1] = '\0';
        } else {
            strncpy(outBuf, "؟", maxLen - 1);
        }
    } else {
        strncpy(outBuf, "انتخاب نشده", maxLen - 1);
        outBuf[maxLen - 1] = '\0';
    }
}
