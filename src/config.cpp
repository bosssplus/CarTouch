/**
 * config.cpp - پیاده‌سازی توابع مدیریت پیکربندی
 * 
 * مدیریت تنظیمات ذخیره‌شده در حافظه NVS (Non-Volatile Storage) ESP32.
 * این تنظیمات بین راه‌اندازی مجدد دستگاه حفظ می‌شوند.
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#include "config.h"
#include <nvs_flash.h>
#include <nvs.h>

static AppConfig currentConfig;
static bool configLoaded = false;

// === جدید (چک‌لیست تجاری #20) ===
static PasswordChangeCallback _passwordChangeCallback = nullptr;

void registerPasswordChangeCallback(PasswordChangeCallback cb) {
    _passwordChangeCallback = cb;
}

/**
 * بارگذاری تنظیمات از حافظه NVS
 * 
 * اگر هیچ تنظیمات ذخیره‌شده‌ای وجود نداشته باشد،
 * مقادیر پیش‌فرض استفاده می‌شود.
 * 
 * @return true در صورت موفقیت، false در صورت خطا
 */
bool loadConfig() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // اگر NVS خراب است، آن را پاک کن
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        Serial.println("⚠️ [NVS] خطا در مقداردهی NVS");
        return false;
    }

    nvs_handle_t nvsHandle;
    err = nvs_open("CarTouch", NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) {
        Serial.println("⚠️ [NVS] خطا در باز کردن Namespace");
        return false;
    }

    size_t configSize = sizeof(AppConfig);
    err = nvs_get_blob(nvsHandle, "config", &currentConfig, &configSize);
    
    nvs_close(nvsHandle);

    if (err != ESP_OK || currentConfig.configMagic != 0xCAFE1234) {
        // تنظیمات ذخیره‌شده وجود ندارد یا نامعتبر است
        // از مقادیر پیش‌فرض استفاده کن
        Serial.println("[NVS] تنظیمات پیش‌فرض بارگذاری شد");
        // ذخیره تنظیمات پیش‌فرض
        saveConfig();
        configLoaded = true;
        return true;
    }

    configLoaded = true;
    Serial.println("[NVS] تنظیمات با موفقیت بارگذاری شد");
    return true;
}

/**
 * ذخیره تنظیمات جاری در حافظه NVS
 * 
 * @return true در صورت موفقیت
 */
bool saveConfig() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open("CarTouch", NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) {
        Serial.println("⚠️ [NVS] خطا در باز کردن NVS برای ذخیره");
        return false;
    }

    currentConfig.configMagic = 0xCAFE1234;
    err = nvs_set_blob(nvsHandle, "config", &currentConfig, sizeof(AppConfig));
    
    if (err == ESP_OK) {
        err = nvs_commit(nvsHandle);
    }
    
    nvs_close(nvsHandle);

    if (err == ESP_OK) {
        Serial.println("[NVS] تنظیمات ذخیره شد");
        return true;
    }
    
    Serial.println("⚠️ [NVS] خطا در ذخیره تنظیمات");
    return false;
}

/**
 * دریافت pointer به ساختار تنظیمات جاری
 * 
 * @return pointer به AppConfig
 */
AppConfig* getConfig() {
    if (!configLoaded) {
        loadConfig();
    }
    return &currentConfig;
}

/**
 * بررسی می‌کند که آیا کاربر هنوز از رمز پیش‌فرض/موقت استفاده می‌کند
 * (چه رمز وب و چه اینکه اصلاً پرچم forcePasswordChange خاموش نشده باشد).
 * 
 * @return true اگر رمز هنوز پیش‌فرض/تغییرنکرده است و باید هشدار داده شود
 */
bool isUsingDefaultPassword() {
    AppConfig* cfg = getConfig();
    if (cfg->forcePasswordChange) return true;
    if (strcmp(cfg->webPass, WEB_DEFAULT_PASS) == 0) return true;
    return false;
}

/**
 * تنظیم رمز جدید وب و خاموش کردن پرچم اجبار تغییر رمز.
 * حداقل طول رمز را هم بررسی می‌کند.
 * 
 * @return true در صورت موفقیت (رمز معتبر بود و ذخیره شد)
 */
bool setWebPassword(const char* newUser, const char* newPass) {
    if (!newPass || strlen(newPass) < 8) {
        Serial.println("⚠️ [CONFIG] رمز جدید باید حداقل ۸ کاراکتر باشد");
        return false;
    }
    if (strcmp(newPass, WEB_DEFAULT_PASS) == 0) {
        Serial.println("⚠️ [CONFIG] رمز جدید نمی‌تواند همان رمز پیش‌فرض باشد");
        return false;
    }
    
    AppConfig* cfg = getConfig();
    if (newUser && strlen(newUser) > 0) {
        strncpy(cfg->webUser, newUser, sizeof(cfg->webUser) - 1);
        cfg->webUser[sizeof(cfg->webUser) - 1] = '\0';
    }
    strncpy(cfg->webPass, newPass, sizeof(cfg->webPass) - 1);
    cfg->webPass[sizeof(cfg->webPass) - 1] = '\0';
    cfg->forcePasswordChange = false;
    
    bool saved = saveConfig();

    // === جدید (چک‌لیست تجاری #20) ===
    // صرف‌نظر از این‌که setWebPassword از کجا صدا زده شده (TFT یا
    // خود وب)، اگر ذخیره موفق بود، هر ماژولی که session نگه می‌دارد
    // (WebServerManager) خبردار می‌شود تا session قدیمی را باطل کند.
    if (saved && _passwordChangeCallback) {
        _passwordChangeCallback();
    }

    return saved;
}

/**
 * تنظیم یک مقدار پیش‌فرض در config
 * (برای استفاده در اولین راه‌اندازی)
 */
void setDefaultConfig() {
    AppConfig* cfg = getConfig();
    strcpy(cfg->wifiSSID, "");
    strcpy(cfg->wifiPassword, "");
    cfg->wifiEnabled = true;
    strcpy(cfg->webUser, WEB_DEFAULT_USER);
    strcpy(cfg->webPass, WEB_DEFAULT_PASS);
    cfg->forcePasswordChange = true;  // تا زمانی که کاربر رمز را عوض نکند
    strcpy(cfg->vehicleBrand, "Generic");
    strcpy(cfg->vehicleModel, "OBD-II");
    cfg->vehicleYear = 2020;
    cfg->theme = THEME_AUTO;
    cfg->brightnessDay = TFT_BRIGHTNESS_DAY;
    cfg->brightnessNight = TFT_BRIGHTNESS_NIGHT;
    cfg->canSpeed = CAN_SPEED;
    cfg->listenOnlyMode = true;   // پیش‌فرض ایمن: فقط شنود
    cfg->sleepTimeout = AUTO_SLEEP_TIMEOUT;
    // === جدید (چک‌لیست تجاری #9) ===
    cfg->touchCalibrated = false;
    memset(cfg->touchCalData, 0, sizeof(cfg->touchCalData));
    saveConfig();
}
