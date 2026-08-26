/**
 * config.h - فایل تنظیمات سراسری پروژه CarTouch
 * 
 * این فایل شامل تمام ثابت‌ها، پین‌ها و تنظیمات قابل تغییر پروژه است.
 * مقادیر پیش‌فرض در اینجا تنظیم شده و کاربر می‌تواند از طریق منوی تنظیمات
 * در صفحه لمسی یا وب آن‌ها را تغییر دهد.
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ======================== پین‌های سخت‌افزاری ========================

// CAN Bus - ترنسیور SN65HVD230
#define PIN_CAN_TX          9       // GPIO9 - CAN Transmit
#define PIN_CAN_RX          6       // GPIO6 - CAN Receive (تغییر از GPIO10 به دلیل تداخل با TFT_CS)

// TFT Display - ILI9341
#define PIN_TFT_CS          10      // GPIO10 - Chip Select
#define PIN_TFT_DC          7       // GPIO7  - Data/Command
#define PIN_TFT_RST         4       // GPIO4  - Reset
#define PIN_TFT_MOSI        11      // GPIO11 - Master Out Slave In
#define PIN_TFT_SCLK        12      // GPIO12 - Serial Clock
#define PIN_TFT_MISO        13      // GPIO13 - Master In Slave Out
#define PIN_TFT_BL          21      // GPIO21 - Backlight

// Touch Screen - XPT2046
#define PIN_TOUCH_CS        14      // GPIO14 - Touch Chip Select

// LED داخلی ESP32 (در صورت وجود)
#define PIN_LED_INTERNAL    38      // GPIO38 - LED داخلی ESP32-S3 DevKit

// ======================== تنظیمات CAN Bus ========================

#define CAN_SPEED           500000  // نرخ CAN: 500Kbps (استاندارد OBD-II)
#define CAN_LISTEN_TIMEOUT  50      // مهلت دریافت پاسخ CAN (میلی‌ثانیه)
#define CAN_MAX_RETRY       3       // حداکثر تلاش برای ارسال مجدد

// ======================== تنظیمات OBD-II ========================

// PID‌های استاندارد OBD-II (SAE J1979)
#define OBD_PID_ENGINE_RPM    0x0C   // دور موتور (RPM)
#define OBD_PID_VEHICLE_SPEED 0x0D   // سرعت خودرو (km/h)
#define OBD_PID_COOLANT_TEMP  0x05   // دمای مایع خنک‌کننده (°C)
#define OBD_PID_BATTERY_VOLT  0x42   // ولتاژ باطری (V) - غیراستاندارد، از طریق CAN ID خاص
#define OBD_PID_THROTTLE_POS  0x11   // موقعیت دریچه گاز (%)
#define OBD_PID_FUEL_LEVEL    0x2F   // سطح سوخت (%)
#define OBD_PID_RUNTIME       0x1F   // زمان روشن بودن موتور

// ======================== تنظیمات صفحه نمایش ========================

#define TFT_WIDTH           240     // عرض صفحه TFT (پیکسل)
#define TFT_HEIGHT          320     // ارتفاع صفحه TFT (پیکسل)
#define TFT_ROTATION        1       // چرخش صفحه (0-3)
#define TFT_BRIGHTNESS_MAX  255     // حداکثر روشنایی
#define TFT_BRIGHTNESS_NIGHT 50     // روشنایی در حالت شب
#define TFT_BRIGHTNESS_DAY  200     // روشنایی در حالت روز

// ======================== تنظیمات LVGL ========================

#define LVGL_TICK_MS        5       // تیک تایمر LVGL (میلی‌ثانیه)
#define LVGL_BUF_SIZE       (TFT_WIDTH * 20) // سایز بافر LVGL

// ======================== تنظیمات WiFi و WebServer ========================

#define WIFI_AP_NAME        "CarTouch"          // نام Access Point
#define WIFI_AP_PASSWORD    "12345678"          // رمز عبور AP (حداقل ۸ کاراکتر)
#define WIFI_MAX_RETRY      20                  // حداکثر تلاش برای اتصال به WiFi
#define WIFI_TIMEOUT_MS     15000               // مهلت اتصال به WiFi (میلی‌ثانیه)

#define WEB_PORT            80                  // پورت وب سرور
#define WS_PORT             81                  // پورت WebSocket

#define WEB_DEFAULT_USER    "admin"             // نام کاربری پیش‌فرض وب
#define WEB_DEFAULT_PASS    "cartouch"          // رمز عبور پیش‌فرض وب

// ======================== تنظیمات مدیریت انرژی ========================

#define AUTO_SLEEP_TIMEOUT  600000              // ۱۰ دقیقه بی‌فعالیتی تا خواب (میلی‌ثانیه)
#define CAN_WAKEUP_ID       0x000               // CAN ID برای بیدار کردن (0x000 = همه)
#define DEEP_SLEEP_WAKEUP_DURATION 60           // بیدار شدن دوره‌ای هر ۶۰ ثانیه در خواب عمیق

// ======================== تنظیمات خودرو ========================

#define MAX_DTC_COUNT       20                  // حداکثر تعداد کدهای خطا
#define CAN_BUS_VOLTAGE_DIVIDER 2.0f            // نسبت تقسیم ولتاژ باطری (در صورت استفاده)

// ======================== انواع داده‌های سراسری ========================

// وضعیت شیشه‌ها
enum WindowState : uint8_t {
    WINDOW_UNKNOWN = 0,
    WINDOW_CLOSED  = 1,
    WINDOW_OPENING = 2,
    WINDOW_CLOSING = 3,
    WINDOW_OPEN    = 4
};

// وضعیت قفل درب‌ها
enum DoorLockState : uint8_t {
    LOCK_UNKNOWN = 0,
    LOCK_LOCKED  = 1,
    LOCK_UNLOCKED = 2
};

// حالت دزدگیر
enum AlarmState : uint8_t {
    ALARM_DISARMED = 0,
    ALARM_ARMED    = 1,
    ALARM_TRIGGERED = 2
};

// حالت شب/روز
enum ThemeMode : uint8_t {
    THEME_DAY   = 0,
    THEME_NIGHT = 1,
    THEME_AUTO  = 2
};

// حالت دستگاه
enum DeviceMode : uint8_t {
    MODE_LISTEN_ONLY = 0,    // فقط شنود - هیچ دستوری ارسال نمی‌شود
    MODE_ACTIVE      = 1,    // فعال - کاربر می‌تواند دستور بدهد
    MODE_SLEEP       = 2,    // خواب - کم‌مصرف
    MODE_DEEP_SLEEP  = 3     // خواب عمیق - بسیار کم‌مصرف
};

// ======================== ساختار داده‌های خودرو ========================

// ساختار اطلاعات زنده خودرو
struct VehicleData {
    // پیش‌رانش
    uint16_t engineRPM = 0;          // دور موتور (RPM)
    uint8_t vehicleSpeed = 0;        // سرعت (km/h)
    int8_t coolantTemp = -40;        // دمای مایع خنک‌کننده (°C)
    float batteryVoltage = 0.0f;     // ولتاژ باطری (V)
    uint8_t throttlePos = 0;         // موقعیت دریچه گاز (%)
    uint8_t fuelLevel = 0;           // سطح سوخت (%)
    uint16_t engineRuntime = 0;      // زمان روشن بودن (ثانیه)
    
    // وضعیت درب‌ها
    DoorLockState doorFL = LOCK_UNKNOWN;   // درب جلو چپ
    DoorLockState doorFR = LOCK_UNKNOWN;   // درب جلو راست
    DoorLockState doorRL = LOCK_UNKNOWN;   // درب عقب چپ
    DoorLockState doorRR = LOCK_UNKNOWN;   // درب عقب راست
    DoorLockState trunkState = LOCK_UNKNOWN; // صندوق عقب
    
    // وضعیت شیشه‌ها
    WindowState windowFL = WINDOW_UNKNOWN;
    WindowState windowFR = WINDOW_UNKNOWN;
    WindowState windowRL = WINDOW_UNKNOWN;
    WindowState windowRR = WINDOW_UNKNOWN;
    WindowState sunroofState = WINDOW_UNKNOWN;
    
    // سایر
    AlarmState alarmState = ALARM_DISARMED;
    bool mirrorFolded = false;
    uint8_t errorCount = 0;
};

// ساختار تنظیمات ذخیره‌شده در EEPROM/NVS
struct AppConfig {
    // WiFi
    char wifiSSID[32] = "";
    char wifiPassword[64] = "";
    bool wifiEnabled = true;
    
    // وب
    char webUser[16] = WEB_DEFAULT_USER;
    char webPass[16] = WEB_DEFAULT_PASS;
    
    // خودرو
    char vehicleBrand[32] = "Generic";
    char vehicleModel[32] = "OBD-II";
    uint16_t vehicleYear = 2020;
    
    // نمایش
    ThemeMode theme = THEME_AUTO;
    uint8_t brightnessDay = TFT_BRIGHTNESS_DAY;
    uint8_t brightnessNight = TFT_BRIGHTNESS_NIGHT;
    
    // CAN
    uint32_t canSpeed = CAN_SPEED;
    bool listenOnlyMode = false;
    
    // انرژی
    uint32_t sleepTimeout = AUTO_SLEEP_TIMEOUT;
    
    // فلگ اعتبارسنجی (برای اطمینان از ذخیره صحیح)
    uint32_t configMagic = 0xCAFE1234;
};

#endif // CONFIG_H
