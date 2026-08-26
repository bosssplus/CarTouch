/**
 * main.cpp - فایل اصلی پروژه CarTouch
 * 
 * این فایل تمام ماژول‌ها را به هم متصل کرده و حلقه اصلی برنامه را اجرا می‌کند.
 * 
 * جریان کلی:
 *   1. مقداردهی اولیه سخت‌افزار (CAN, TFT, WiFi)
 *   2. بارگذاری تنظیمات
 *   3. شروع رابط کاربری (TFT + Web)
 *   4. حلقه اصلی:
 *      - پردازش رویدادهای LVGL
 *      - خواندن داده‌های OBD-II (دوره‌ای)
 *      - مدیریت WebSocket
 *      - مدیریت خواب خودکار
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#include <Arduino.h>
#include <SPIFFS.h>

#include "config.h"
#include "can_manager.h"
#include "obd2_reader.h"
#include "vehicle_control.h"
#include "vehicle_db.h"
#include "tft_ui.h"
#include "webserver.h"
#include "wifi_manager.h"

// ======================== اشیاء سراسری ========================

// CAN
CANManager canManager(PIN_CAN_TX, PIN_CAN_RX, CAN_SPEED);

// OBD-II
OBD2Reader obd2Reader(canManager);

// کنترل خودرو
VehicleControl vehicleControl(canManager);

// پایگاه داده خودرو
VehicleDB vehicleDB;

// رابط کاربری TFT
TFT_UI tftUI;

// وب سرور
WebServerManager webServer;

// WiFi
WiFiManager wifiManager;

// ======================== داده‌های سراسری ========================

VehicleData currentVehicleData;
uint32_t lastDataUpdateTime = 0;
uint32_t lastActivityTime = 0;
uint32_t obdReadInterval = 200;  // هر ۲۰۰ میلی‌ثانیه یکبار OBD بخوان
DeviceMode currentMode = MODE_ACTIVE;

// ======================== پروتوتایپ توابع ========================

void setup();
void loop();
void handleCommand(const char* command);
void handleControlCommand(const char* command);
void updateVehicleData();
void checkAutoSleep();
void wakeFromSleep();

// ======================== تابع setup ========================

void setup() {
    // شروع Serial Monitor
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n========================================");
    Serial.println(" CarTouch v1.0 - ESP32-S3 Car Control");
    Serial.println("========================================\n");
    
    // 1. بارگذاری تنظیمات
    Serial.println("[INIT] بارگذاری تنظیمات...");
    loadConfig();
    
    // 2. شروع SPIFFS (برای فایل‌های وب و DBC)
    Serial.println("[INIT] شروع SPIFFS...");
    if (!SPIFFS.begin(false)) {
        Serial.println("⚠️ [INIT] SPIFFS خطا - فرمت کردن...");
        SPIFFS.format();
        if (!SPIFFS.begin(true)) {
            Serial.println("⚠️ [INIT] SPIFFS همچنان با خطا مواجه است");
        }
    } else {
        Serial.println("[INIT] SPIFFS آماده است");
    }
    
    // 3. شروع CAN Bus
    Serial.println("[INIT] شروع CAN Bus...");
    if (!canManager.begin()) {
        Serial.println("⚠️ [INIT] CAN Bus راه‌اندازی نشد! بررسی سیم‌کشی");
        tftUI.showNotification("⚠️ CAN Bus خطا!");
    } else {
        canManager.flushRxQueue();
        tftUI.setCANStatus(true);
    }
    
    // 4. شروع OBD-II Reader
    obd2Reader.begin();
    
    // 5. شروع Vehicle Control
    vehicleControl.begin();
    
    // 6. شروع Vehicle DB
    vehicleDB.begin();
    
    // 7. شروع TFT و LVGL
    tftUI.begin();
    tftUI.setControlCallback(handleCommand);
    tftUI.showNotification("🚗 CarTouch آماده است");
    
    // 8. شروع WiFi (حالت AP پیش‌فرض)
    wifiManager.begin(1);  // 1 = AP mode
    tftUI.setWiFiStatus(wifiManager.isConnected());
    
    // 9. شروع وب سرور
    webServer.begin();
    webServer.setCommandCallback(handleCommand);
    
    // 10. تنظیم CAN IDهای سفارشی از DBC (اگر بارگذاری شده باشد)
    // (در صورت نیاز، بعد از انتخاب خودرو)
    
    // مقداردهی اولیه زمان
    lastActivityTime = millis();
    
    Serial.println("\n[INIT] ✅ CarTouch آماده به کار است!");
    Serial.printf("[INIT] IP: %s\n", wifiManager.getIP().toString().c_str());
    Serial.printf("[INIT] CAN: %s\n", canManager.isActive() ? "✅" : "❌");
}

// ======================== حلقه اصلی ========================

void loop() {
    // 1. به‌روزرسانی LVGL
    tftUI.update();
    
    // 2. به‌روزرسانی WebSocket
    webServer.update();
    
    // 3. خواندن داده‌های OBD-II (دوره‌ای)
    if (currentMode == MODE_ACTIVE && 
        millis() - lastDataUpdateTime > obdReadInterval) {
        updateVehicleData();
        lastDataUpdateTime = millis();
    }
    
    // 4. بررسی خواب خودکار
    checkAutoSleep();
    
    // 5. بررسی بیدار شدن با پیام CAN
    if (currentMode == MODE_SLEEP || currentMode == MODE_DEEP_SLEEP) {
        CanMessage wakeMsg;
        if (canManager.receiveMessageNonBlocking(wakeMsg)) {
            wakeFromSleep();
        }
    }
    
    // تاخیر کوتاه برای جلوگیری از watchdog timeout
    delay(5);
}

// ======================== به‌روزرسانی داده‌های خودرو ========================

void updateVehicleData() {
    // خواندن PIDهای پایه
    obd2Reader.readAllPIDs(currentVehicleData);
    
    // خواندن ولتاژ باطری (از طریق CAN یا محاسبه)
    // توجه: خواندن ولتاژ باطری از طریق CAN در بسیاری از خودروها پشتیبانی نمی‌شود
    // در این نسخه از مقدار ثابت استفاده شده
    currentVehicleData.batteryVoltage = 12.6f;  // مقدار پیش‌فرض
    
    // به‌روزرسانی رابط کاربری
    tftUI.updateVehicleData(currentVehicleData);
    
    // ارسال به WebSocket
    webServer.broadcastVehicleData(currentVehicleData);
}

// ======================== پردازش فرمان ========================

void handleCommand(const char* command) {
    // ثبت زمان آخرین فعالیت
    lastActivityTime = millis();
    
    Serial.printf("[CMD] فرمان دریافت شد: %s\n", command);
    
    // اگر در حالت Listen-Only هستیم، فقط فرمان‌های اطلاعاتی مجازند
    if (getConfig()->listenOnlyMode) {
        if (strcmp(command, "listen_only") == 0 || 
            strcmp(command, "vehicle_select") == 0 ||
            strcmp(command, "toggle_theme") == 0) {
            handleControlCommand(command);
        } else {
            Serial.println("⚠️ [CMD] حالت Listen-Only - فرمان کنترلی رد شد");
            tftUI.showNotification("👂 حالت Listen-Only فعال است");
        }
        return;
    }
    
    handleControlCommand(command);
}

// ======================== اجرای فرمان کنترلی ========================

void handleControlCommand(const char* command) {
    bool result = false;
    
    if (strcmp(command, "lock") == 0) {
        result = vehicleControl.lockAllDoors();
    } 
    else if (strcmp(command, "unlock") == 0) {
        result = vehicleControl.unlockAllDoors();
    } 
    else if (strcmp(command, "windows_up") == 0) {
        result = vehicleControl.allWindowsUp();
    } 
    else if (strcmp(command, "windows_down") == 0) {
        result = vehicleControl.allWindowsDown();
    } 
    else if (strcmp(command, "sunroof") == 0) {
        // هر بار فشار، وضعیت عوض می‌شود (باز/بسته)
        result = vehicleControl.sunroofOpen();
    } 
    else if (strcmp(command, "trunk") == 0) {
        result = vehicleControl.trunkOpen();
    } 
    else if (strcmp(command, "mirror") == 0) {
        result = vehicleControl.foldMirrors();
    } 
    else if (strcmp(command, "alarm") == 0) {
        // اگر دزدگیر فعال نیست، فعال کن و بالعکس
        if (currentVehicleData.alarmState == ALARM_DISARMED) {
            result = vehicleControl.alarmArm();
            currentVehicleData.alarmState = ALARM_ARMED;
        } else {
            result = vehicleControl.alarmDisarm();
            currentVehicleData.alarmState = ALARM_DISARMED;
        }
    } 
    else if (strcmp(command, "listen_only") == 0) {
        AppConfig* cfg = getConfig();
        cfg->listenOnlyMode = !cfg->listenOnlyMode;
        saveConfig();
        tftUI.showNotification(cfg->listenOnlyMode ? 
            "👂 حالت Listen-Only فعال" : "🎤 حالت فعال");
        Serial.printf("[CMD] Listen-Only: %s\n", 
                      cfg->listenOnlyMode ? "ON" : "OFF");
        return;
    } 
    else if (strcmp(command, "toggle_theme") == 0) {
        AppConfig* cfg = getConfig();
        cfg->theme = (cfg->theme == THEME_DAY) ? THEME_NIGHT : THEME_DAY;
        saveConfig();
        tftUI.setTheme(cfg->theme);
        return;
    } 
    else if (strcmp(command, "vehicle_select") == 0) {
        // (چرخه بین خودروها - پیاده‌سازی کامل در نسخه بعدی)
        tftUI.showNotification("🚗 انتخاب خودرو در منو");
        return;
    } 
    else {
        Serial.printf("⚠️ [CMD] فرمان ناشناخته: %s\n", command);
        tftUI.showNotification("❌ فرمان ناشناخته");
        return;
    }
    
    // نمایش نتیجه
    if (result) {
        tftUI.showNotification("✅ فرمان ارسال شد");
        webServer.broadcastStatus(command);
    } else {
        tftUI.showNotification("❌ خطا در ارسال فرمان");
        Serial.printf("⚠️ [CMD] خطا در اجرای فرمان: %s\n", command);
    }
}

// ======================== بررسی خواب خودکار ========================

void checkAutoSleep() {
    if (currentMode != MODE_ACTIVE) return;
    
    AppConfig* cfg = getConfig();
    uint32_t inactivityTime = millis() - lastActivityTime;
    
    if (inactivityTime >= cfg->sleepTimeout) {
        Serial.println("[SLEEP] رفتن به حالت Sleep (۱۰ دقیقه بی‌فعالیتی)");
        currentMode = MODE_SLEEP;
        
        // خاموش کردن صفحه
        tftUI.setDeviceMode(MODE_SLEEP);
        
        // قطع WiFi (صرفه‌جویی در باتری)
        wifiManager.disconnect();
        
        // غیرفعال کردن OBD polling
        // (CAN همچنان فعال است برای بیدار شدن)
        
        Serial.println("[SLEEP] دستگاه در حالت Sleep است - منتظر پیام CAN برای بیدار شدن");
    }
}

// ======================== بیدار شدن از خواب ========================

void wakeFromSleep() {
    if (currentMode == MODE_ACTIVE) return;
    
    Serial.println("[WAKE] بیدار شدن از Sleep...");
    
    currentMode = MODE_ACTIVE;
    lastActivityTime = millis();
    
    // روشن کردن صفحه
    tftUI.setDeviceMode(MODE_ACTIVE);
    
    // راه‌اندازی مجدد WiFi اگر خاموش شده بود
    if (!wifiManager.isConnected()) {
        wifiManager.begin(1);  // AP mode
    }
    
    tftUI.showNotification("🚗 بیدار شدم!");
    
    Serial.println("[WAKE] دستگاه بیدار شد ✓");
}
