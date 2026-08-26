/**
 * wifi_manager.cpp - پیاده‌سازی مدیریت WiFi
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#include "wifi_manager.h"

// ======================== سازنده ========================

WiFiManager::WiFiManager() {
    _state = WIFI_DISABLED;
    _enabled = true;
}

// ======================== شروع ========================

void WiFiManager::begin(uint8_t mode) {
    if (!_enabled) {
        Serial.println("[WiFi] WiFi غیرفعال است");
        _state = WIFI_DISABLED;
        return;
    }
    
    Serial.println("[WiFi] شروع WiFi...");
    
    switch (mode) {
        case 0:
            WiFi.mode(WIFI_OFF);
            _state = WIFI_DISABLED;
            Serial.println("[WiFi] WiFi خاموش شد");
            break;
            
        case 1:
            _startAP();
            break;
            
        case 2:
            _startSTA();
            break;
            
        default:
            _startAP();
            break;
    }
}

// ======================== شروع Access Point ========================

void WiFiManager::_startAP() {
    WiFi.mode(WIFI_AP);
    
    // تنظیم نام و رمز AP
    bool result = WiFi.softAP(WIFI_AP_NAME, WIFI_AP_PASSWORD);
    
    if (result) {
        _state = WIFI_AP;
        Serial.printf("[WiFi] Access Point: %s | IP: %s\n", 
                      WIFI_AP_NAME, WiFi.softAPIP().toString().c_str());
        Serial.printf("[WiFi] رمز: %s\n", WIFI_AP_PASSWORD);
    } else {
        _state = WIFI_DISABLED;
        Serial.println("⚠️ [WiFi] خطا در ایجاد Access Point");
    }
}

// ======================== شروع Station ========================

void WiFiManager::_startSTA() {
    AppConfig* cfg = getConfig();
    
    // اگر SSID ذخیره نشده، به AP برو
    if (strlen(cfg->wifiSSID) == 0) {
        Serial.println("[WiFi] SSID ذخیره نشده - استفاده از AP");
        _startAP();
        return;
    }
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg->wifiSSID, cfg->wifiPassword);
    
    Serial.printf("[WiFi] اتصال به %s...\n", cfg->wifiSSID);
    
    // انتظار برای اتصال
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < WIFI_MAX_RETRY) {
        delay(500);
        Serial.print(".");
        retry++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        _state = WIFI_STA;
        Serial.printf("\n[WiFi] متصل شد! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        _state = WIFI_STA_FAIL;
        Serial.println("\n⚠️ [WiFi] اتصال ناموفق - استفاده از AP");
        _startAP();
    }
}

// ======================== قطع ========================

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _state = WIFI_DISABLED;
    Serial.println("[WiFi] WiFi قطع شد");
}

// ======================== اسکن شبکه‌ها ========================

uint8_t WiFiManager::scanNetworks(char networks[][32], uint8_t maxCount) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    int count = WiFi.scanNetworks();
    if (count < 0) {
        Serial.println("⚠️ [WiFi] خطا در اسکن");
        return 0;
    }
    
    Serial.printf("[WiFi] %d شبکه یافت شد\n", count);
    
    uint8_t result = 0;
    for (int i = 0; i < count && result < maxCount; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() > 0) {
            strncpy(networks[result], ssid.c_str(), 32);
            networks[result][31] = '\0';
            Serial.printf("  %d: %s\n", result + 1, networks[result]);
            result++;
        }
    }
    
    return result;
}

// ======================== اتصال به شبکه ========================

bool WiFiManager::connectToNetwork(const char* ssid, const char* password) {
    // ذخیره در تنظیمات
    AppConfig* cfg = getConfig();
    strncpy(cfg->wifiSSID, ssid, sizeof(cfg->wifiSSID) - 1);
    strncpy(cfg->wifiPassword, password, sizeof(cfg->wifiPassword) - 1);
    saveConfig();
    
    // اتصال
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < WIFI_MAX_RETRY) {
        delay(500);
        retry++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        _state = WIFI_STA;
        Serial.printf("[WiFi] به %s متصل شد. IP: %s\n", ssid, WiFi.localIP().toString().c_str());
        return true;
    }
    
    Serial.printf("⚠️ [WiFi] اتصال به %s ناموفق\n", ssid);
    _state = WIFI_STA_FAIL;
    return false;
}

// ======================== وضعیت ========================

WiFiState WiFiManager::getState() {
    return _state;
}

// ======================== IP ========================

IPAddress WiFiManager::getIP() {
    if (_state == WIFI_AP) {
        return WiFi.softAPIP();
    } else if (_state == WIFI_STA) {
        return WiFi.localIP();
    }
    return IPAddress(0, 0, 0, 0);
}

// ======================== بررسی اتصال ========================

bool WiFiManager::isConnected() {
    return (_state == WIFI_STA && WiFi.status() == WL_CONNECTED) ||
           (_state == WIFI_AP);
}

// ======================== روشن بودن ========================

bool WiFiManager::isEnabled() {
    return _enabled;
}

// ======================== تنظیم روشن/خاموش ========================

void WiFiManager::setEnabled(bool enabled) {
    _enabled = enabled;
    if (!enabled) {
        disconnect();
    }
}
