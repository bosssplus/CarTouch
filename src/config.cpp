/**
 * config.cpp - Configuration management implementation
 *
 * Settings are persisted in the ESP32's NVS (Non-Volatile Storage) and
 * survive reboots.
 */

#include "config.h"
#include <nvs_flash.h>
#include <nvs.h>

static AppConfig currentConfig;
static bool       configLoaded = false;

static PasswordChangeCallback _passwordChangeCallback = nullptr;

void registerPasswordChangeCallback(PasswordChangeCallback cb) {
    _passwordChangeCallback = cb;
}

// ============================================================================
// Load / save
// ============================================================================

bool loadConfig() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition is corrupt or outdated - erase and retry
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        Serial.println("[NVS] Init failed");
        return false;
    }

    nvs_handle_t nvsHandle;
    err = nvs_open("CarTouch", NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) {
        Serial.println("[NVS] Failed to open namespace");
        return false;
    }

    size_t configSize = sizeof(AppConfig);
    err = nvs_get_blob(nvsHandle, "config", &currentConfig, &configSize);

    nvs_close(nvsHandle);

    if (err != ESP_OK || currentConfig.configMagic != 0xCAFE1234) {
        // No stored config, or it's invalid - fall back to defaults
        Serial.println("[NVS] Loading default configuration");
        saveConfig();
        configLoaded = true;
        return true;
    }

    configLoaded = true;
    Serial.println("[NVS] Configuration loaded");
    return true;
}

bool saveConfig() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open("CarTouch", NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) {
        Serial.println("[NVS] Failed to open for saving");
        return false;
    }

    currentConfig.configMagic = 0xCAFE1234;
    err = nvs_set_blob(nvsHandle, "config", &currentConfig, sizeof(AppConfig));

    if (err == ESP_OK) {
        err = nvs_commit(nvsHandle);
    }

    nvs_close(nvsHandle);

    if (err == ESP_OK) {
        Serial.println("[NVS] Configuration saved");
        return true;
    }

    Serial.println("[NVS] Save failed");
    return false;
}

AppConfig* getConfig() {
    if (!configLoaded) {
        loadConfig();
    }
    return &currentConfig;
}

// ============================================================================
// Password helpers
// ============================================================================

bool isUsingDefaultPassword() {
    AppConfig* cfg = getConfig();
    if (cfg->forcePasswordChange) return true;
    if (strcmp(cfg->webPass, WEB_DEFAULT_PASS) == 0) return true;
    return false;
}

bool setWebPassword(const char* newUser, const char* newPass) {
    if (!newPass || strlen(newPass) < 8) {
        Serial.println("[CONFIG] New password must be at least 8 characters");
        return false;
    }
    if (strcmp(newPass, WEB_DEFAULT_PASS) == 0) {
        Serial.println("[CONFIG] New password cannot match the default");
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

    // Regardless of which interface called this (TFT or web), notify any
    // module holding its own session state so it can invalidate stale
    // sessions right away.
    if (saved && _passwordChangeCallback) {
        _passwordChangeCallback();
    }

    return saved;
}

// ============================================================================
// Defaults
// ============================================================================

void setDefaultConfig() {
    AppConfig* cfg = getConfig();
    strcpy(cfg->wifiSSID, "");
    strcpy(cfg->wifiPassword, "");
    cfg->wifiEnabled = true;
    strcpy(cfg->webUser, WEB_DEFAULT_USER);
    strcpy(cfg->webPass, WEB_DEFAULT_PASS);
    cfg->forcePasswordChange = true;
    strcpy(cfg->vehicleBrand, "Generic");
    strcpy(cfg->vehicleModel, "OBD-II");
    cfg->vehicleYear = 2020;
    cfg->theme = THEME_AUTO;
    cfg->brightnessDay = TFT_BRIGHTNESS_DAY;
    cfg->brightnessNight = TFT_BRIGHTNESS_NIGHT;
    cfg->canSpeed = CAN_SPEED;
    cfg->listenOnlyMode = true;   // Safe default: listen-only
    cfg->sleepTimeout = AUTO_SLEEP_TIMEOUT;
    cfg->touchCalibrated = false;
    memset(cfg->touchCalData, 0, sizeof(cfg->touchCalData));
    saveConfig();
}
