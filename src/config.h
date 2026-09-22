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

// TFT Display - ILI9341 (نمایشگر لمسی SPI چهار سیمه ۲.۸ اینچ، 240x320)
// اصلاحیه: پیش‌تر درایور در platformio.ini به اشتباه ST7796_DRIVER تنظیم
// شده بود (و یک کامنت قبلی هم به همین اشتباه "تصحیح" شده بود). درایور
// واقعی سخت‌افزار ILI9341 است و اکنون در platformio.ini با
// -DILI9341_DRIVER=1 تنظیم شده. پین‌های زیر همچنان با platformio.ini
// هماهنگ و درست هستند.
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
#define WIFI_AP_PASSWORD    "12345678"          // رمز موقت AP - در اولین فرصت از منوی تنظیمات عوض شود (حداقل ۸ کاراکتر)
#define WIFI_MAX_RETRY      20                  // حداکثر تلاش برای اتصال به WiFi
#define WIFI_TIMEOUT_MS     15000               // مهلت اتصال به WiFi (میلی‌ثانیه)

#define WEB_PORT            80                  // پورت وب سرور
#define WS_PORT             81                  // پورت WebSocket

// === اصلاحیه امنیتی ===
// رمزهای پیش‌فرض قبلی ("admin"/"cartouch" و AP "12345678") برای همه
// نسخه‌های این پروژه یکسان بودند و در همین ریپوی عمومی گیت‌هاب قابل مشاهده‌اند؛
// یعنی هرکسی که بداند شما از CarTouch استفاده می‌کنید می‌تواند این رمزها را
// امتحان کند. بنابراین:
//   ۱. این‌ها فقط "مقدار موقت اولیه" هستند، نه رمز نهایی.
//   ۲. پرچم forcePasswordChange در AppConfig در اولین بوت true است و
//      تا وقتی کاربر از منوی تنظیمات (TFT) یا صفحه وب رمز را عوض نکند،
//      دستگاه هر بار یک هشدار برجسته نمایش می‌دهد (به‌جای رد کامل
//      دسترسی، چون ممکن است کاربر برای اولین‌بار به تنظیمات نیاز داشته
//      باشد که همان رمز موقت را وارد کند).
#define WEB_DEFAULT_USER    "admin"             // نام کاربری موقت اولیه
#define WEB_DEFAULT_PASS    "cartouch"          // رمز موقت اولیه - باید در اولین اجرا عوض شود

// ======================== تنظیمات مدیریت انرژی ========================

#define AUTO_SLEEP_TIMEOUT  600000              // ۱۰ دقیقه بی‌فعالیتی تا خواب (میلی‌ثانیه)
#define CAN_WAKEUP_ID       0x000               // CAN ID برای بیدار کردن (0x000 = همه)
#define DEEP_SLEEP_WAKEUP_DURATION 60           // بیدار شدن دوره‌ای هر ۶۰ ثانیه در خواب عمیق

// ======================== تنظیمات خودرو ========================

#define MAX_DTC_COUNT       20                  // حداکثر تعداد کدهای خطا
#define CAN_BUS_VOLTAGE_DIVIDER 2.0f            // نسبت تقسیم ولتاژ باطری (در صورت استفاده)

// ======================== تنظیمات Learn Mode (CarTouch v2.0) ========================
// به CarTouch_V2_SPEC.md بخش ۴.۳ و ۷.۷ مراجعه کنید.
// این بخش جدید است و به قابلیت‌های v1.0 آسیبی نمی‌زند.

#define MAX_CUSTOM_VEHICLES               8     // حداکثر تعداد پروفایل‌های سفارشی (Learned/Manual)
#define MAX_LEARNED_COMMANDS_PER_VEHICLE  32     // حداکثر فرمان در هر پروفایل سفارشی
#define LEARN_BASELINE_MS               2000     // مدت ضبط پیش‌زمینه پیش‌فرض (میلی‌ثانیه)
#define LEARN_ACTION_CAPTURE_MS         2000     // مدت ضبط اقدام پیش‌فرض (میلی‌ثانیه)
#define LEARN_ACTION_CAPTURE_MAX_MS     5000     // حداکثر مجاز مدت ضبط اقدام (میلی‌ثانیه)
#define BASELINE_MAX_IDS                  64     // حداکثر CAN ID متمایز قابل ردیابی در بازه‌ی baseline
#define CANDIDATE_MAX                     10     // حداکثر کاندید نمایش‌داده‌شده بعد از diff

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
    bool forcePasswordChange = true;  // تا کاربر رمز را عوض نکند true می‌ماند
    
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
    bool listenOnlyMode = true;   // پیش‌فرض ایمن: فقط شنود، هیچ فریمی روی باس ارسال نمی‌شود
    
    // انرژی
    uint32_t sleepTimeout = AUTO_SLEEP_TIMEOUT;

    // === جدید (چک‌لیست تجاری #9: کالیبراسیون واقعی تاچ‌اسکرین) ===
    // خروجی calibrateTouch() کتابخانه‌ی TFT_eSPI دقیقاً ۵ مقدار
    // uint16_t است (calData[5]) که رابطه‌ی بین مختصات خام ADC و
    // مختصات پیکسل صفحه را مشخص می‌کند. قبلاً این مقادیر هرگز واقعاً
    // در جایی محاسبه یا ذخیره نمی‌شدند - فقط یک آرایه‌ی ثابت و حدسی
    // (touchCalibX/Y در tft_ui.cpp) بود که اصلاً استفاده هم نمی‌شد.
    uint16_t touchCalData[5] = {0, 0, 0, 0, 0};
    bool touchCalibrated = false;  // false = هنوز کالیبره نشده، اولین بوت باید ویزارد کالیبراسیون نشان دهد
    
    // فلگ اعتبارسنجی (برای اطمینان از ذخیره صحیح)
    uint32_t configMagic = 0xCAFE1234;
};

// ======================== توابع اصلی مدیریت پیکربندی ========================
// توجه: این پروتوتایپ‌ها در نسخه اصلی در config.h وجود نداشتند و توابع
// فقط در config.cpp تعریف شده بودند. در C++ این باعث خطای کامپایل
// می‌تواند بشود اگر فایلی زودتر از تعریف تابع را استفاده کند
// (implicit function declaration در C++ مدرن مجاز نیست).

/**
 * بارگذاری تنظیمات از NVS. اگر تنظیمات معتبر نباشد، پیش‌فرض را می‌سازد.
 */
bool loadConfig();

/**
 * ذخیره تنظیمات جاری در NVS
 */
bool saveConfig();

/**
 * دریافت pointer به تنظیمات جاری (در صورت نیاز بارگذاری می‌کند)
 */
AppConfig* getConfig();

/**
 * بازنشانی تنظیمات به مقادیر پیش‌فرض کارخانه
 */
void setDefaultConfig();

// ======================== توابع کمکی امنیت رمز ========================

/**
 * بررسی اینکه آیا هنوز از رمز پیش‌فرض/موقت استفاده می‌شود
 */
bool isUsingDefaultPassword();

/**
 * تنظیم رمز جدید وب (حداقل ۸ کاراکتر، متفاوت از پیش‌فرض)
 * @return true در صورت موفقیت
 */
bool setWebPassword(const char* newUser, const char* newPass);

// === جدید (چک‌لیست تجاری #20: همگام‌سازی session بین TFT و وب) ===
// مشکل قبلی: setWebPassword از دو مسیر مستقل صدا زده می‌شد - یک بار
// از webserver.cpp (که خودش بعدش _sessionToken داخلی‌اش را باطل
// می‌کرد) و یک بار از tft_ui.cpp (که هیچ راهی برای باطل کردن session
// وب نداشت، چون _sessionToken یک متغیر خصوصی در کلاس دیگری است).
// نتیجه: اگر کاربر رمز را از صفحه‌ی لمسی عوض می‌کرد، یک session وب
// که با رمز *قدیمی* لاگین کرده بود همچنان معتبر می‌ماند.
//
// راه‌حل: یک نقطه‌ی callback سراسری و اختیاری. هر ماژولی که session
// نگه می‌دارد (فعلاً فقط WebServerManager) با
// registerPasswordChangeCallback یک تابع ثبت می‌کند؛ setWebPassword
// در صورت موفقیت، این callback را صدا می‌زند - از هر مسیری
// (TFT یا وب) که فراخوانی شده باشد.
typedef void (*PasswordChangeCallback)();

/**
 * ثبت یک callback که هر بار setWebPassword موفق شود صدا زده می‌شود.
 * برای همگام‌سازی session بین رابط‌های مختلف (TFT/وب) استفاده می‌شود.
 * فقط یک callback همزمان پشتیبانی می‌شود (کافی برای معماری فعلی که
 * فقط WebServerManager session نگه می‌دارد).
 */
void registerPasswordChangeCallback(PasswordChangeCallback cb);

#endif // CONFIG_H
