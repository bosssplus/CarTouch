/**
 * wifi_manager.h - مدیریت اتصال WiFi
 * 
 * این ماژول وظیفه اتصال ESP32 به شبکه WiFi یا ایجاد Access Point را دارد.
 * در حالت AP، دستگاه به صورت مستقیم قابل دسترسی است.
 * در حالت STA، دستگاه به شبکه خانگی/محل کار متصل می‌شود.
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

// وضعیت‌های WiFi
enum WiFiState : uint8_t {
    WIFI_DISABLED = 0,
    WIFI_AP       = 1,     // Access Point mode
    WIFI_STA      = 2,     // Station mode (connected to router)
    WIFI_STA_FAIL = 3      // Station mode failed to connect
};

/**
 * کلاس مدیریت WiFi
 */
class WiFiManager {
public:
    WiFiManager();
    
    /**
     * شروع WiFi
     * @param mode حالت: 0=خاموش, 1=AP, 2=STA (با تلاش برای اتصال)
     */
    void begin(uint8_t mode = 1);
    
    /**
     * قطع WiFi
     */
    void disconnect();
    
    /**
     * اسکن شبکه‌های موجود
     * @param networks [out] آرایه برای ذخیره SSIDها
     * @param maxCount حداکثر تعداد
     * @return تعداد شبکه‌های یافت شده
     */
    uint8_t scanNetworks(char networks[][32], uint8_t maxCount = 10);
    
    /**
     * اتصال به یک شبکه
     * @param ssid نام شبکه
     * @param password رمز عبور
     * @return true در صورت موفقیت
     */
    bool connectToNetwork(const char* ssid, const char* password);
    
    /**
     * دریافت وضعیت جاری
     */
    WiFiState getState();
    
    /**
     * دریافت آدرس IP (در حالت AP یا STA)
     */
    IPAddress getIP();
    
    /**
     * بررسی اتصال
     */
    bool isConnected();
    
    /**
     * بررسی روشن بودن WiFi
     */
    bool isEnabled();
    
    /**
     * تنظیم روشن/خاموش
     */
    void setEnabled(bool enabled);

private:
    WiFiState _state;
    bool _enabled;
    
    // شروع Access Point
    void _startAP();
    
    // شروع Station
    void _startSTA();
};

#endif // WIFI_MANAGER_H
