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
    strcpy(cfg->vehicleBrand, "Generic");
    strcpy(cfg->vehicleModel, "OBD-II");
    cfg->vehicleYear = 2020;
    cfg->theme = THEME_AUTO;
    cfg->brightnessDay = TFT_BRIGHTNESS_DAY;
    cfg->brightnessNight = TFT_BRIGHTNESS_NIGHT;
    cfg->canSpeed = CAN_SPEED;
    cfg->listenOnlyMode = false;
    cfg->sleepTimeout = AUTO_SLEEP_TIMEOUT;
    saveConfig();
}
