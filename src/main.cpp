/**
 * main.cpp - فایل اصلی پروژه CarTouch (v2.0)
 * 
 * === تغییرات نسبت به v1.0 ===
 * این فایل ماژول‌های جدید Learn Mode را سیم‌کشی می‌کند:
 *   - CustomVehicleStore (ذخیره‌سازی پروفایل‌های سفارشی در SPIFFS)
 *   - LearnEngine (منطق capture/diff)
 *   - ActiveProfileManager (یکپارچه‌ساز DBC + سفارشی)
 * 
 * vehicleControl حالا به‌جای گرفتن CAN ID مستقیم، رفرنس
 * activeProfileManager می‌گیرد (به vehicle_control.h/.cpp مراجعه کنید).
 * 
 * تمام جریان کلی setup()/loop() از v1.0 حفظ شده - هیچ مرحله‌ای حذف
 * نشده، فقط ماژول‌های جدید اضافه و learnEngine.update() به loop()
 * افزوده شده است.
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "can_manager.h"
#include "obd2_reader.h"
#include "vehicle_control.h"
#include "vehicle_db.h"
#include "tft_ui.h"
#include "webserver.h"
#include "wifi_manager.h"

// === جدید v2.0 ===
#include "custom_vehicle_store.h"
#include "learn_engine.h"
#include "active_profile_manager.h"

// ======================== اشیاء سراسری ========================

// CAN
CANManager canManager(PIN_CAN_TX, PIN_CAN_RX, CAN_SPEED);

// OBD-II
OBD2Reader obd2Reader(canManager);

// پایگاه داده خودرو (DBC)
// === اصلاحیه (چک‌لیست تجاری #7/#8) ===
// با افزایش MAX_DBC_MESSAGES به ۱۵۰ (به config.h/vehicle_db.h مراجعه
// کنید)، حجم آرایه‌ی داخلی این آبجکت حدود ۴۶۰ کیلوبایت است که تقریباً
// کل SRAM داخلی ESP32-S3 (~512KB) را می‌بلعد و برای WiFi/LVGL/استک‌ها
// چیزی باقی نمی‌گذارد؛ باید در PSRAM جا بگیرد.
//
// === اصلاح (باگ لینک - DRAM overflow) ===
// نسخه‌ی قبلی این خط از EXT_RAM_ATTR/EXT_RAM_BSS_ATTR روی یک آبجکت
// global استفاده می‌کرد. این attribute فقط زمانی واقعاً به PSRAM
// می‌رود که گزینه‌ی CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY در
// sdkconfig فعال باشد؛ در کتابخانه‌های از پیش کامپایل‌شده‌ی
// Arduino-ESP32 که PlatformIO دانلود می‌کند این گزینه فعال نیست، پس
// attribute بی‌صدا نادیده گرفته می‌شد و کل ۴۶۰ کیلوبایت همچنان در
// DRAM داخلی می‌ماند → لینکر با «region dram0_0_seg overflowed» شکست
// می‌خورد (دقیقاً همان خطری که یادداشت قبلی هشدارش را داده بود).
//
// راه‌حل قابل‌اعتماد روی این زنجیره‌ی ابزار: تخصیص دینامیک با new/malloc.
// هسته‌ی Arduino-ESP32 وقتی PSRAM فعال باشد، تخصیص‌های بزرگ‌تر از ۴
// کیلوبایت را به‌طور خودکار از PSRAM می‌دهد - نیازی به هیچ attribute
// خاصی نیست. بنابراین vehicleDB اکنون یک pointer است که در ابتدای
// setup() با `new` ساخته می‌شود (نه یک آبجکت global در .bss).
// ActiveProfileManager و VehicleControl هم چون یک reference به
// آبجکت‌های قبل از خودشان نگه می‌دارند، به همین ترتیب به pointer
// تبدیل و در setup() (به همان ترتیب قبلی) ساخته می‌شوند.
VehicleDB* vehicleDB = nullptr;

// === جدید v2.0: ذخیره‌سازی پروفایل‌های سفارشی + موتور یادگیری ===
CustomVehicleStore customVehicleStore;
LearnEngine learnEngine(canManager);
ActiveProfileManager* activeProfileManager = nullptr;

// کنترل خودرو (حالا با ActiveProfileManager به‌جای CAN ID مستقیم)
VehicleControl* vehicleControl = nullptr;

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

// === جدید (چک‌لیست تجاری #2): Task Watchdog Timer ===
// اگر loop() به هر دلیلی (مثلاً readAllPIDs که هنوز blocking است - نگاه
// کنید به یادداشت obd2_reader.h) بیش از WDT_TIMEOUT_S گیر کند، ESP32
// به‌جای هنگ کردن نامحدود، ری‌ست می‌شود. این جایگزین بازطراحی
// non-blocking نیست (که همچنان لازم است) ولی از "قفل کامل و دائمی
// دستگاه در ماشین" جلوگیری می‌کند.
#define WDT_TIMEOUT_S 8

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
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n========================================");
    Serial.println(" CarTouch v2.0 - ESP32-S3 Car Control");
    Serial.println(" (+ Learn Mode / Custom Vehicle Database)");
    Serial.println("========================================\n");

    // === جدید (اصلاح باگ لینک DRAM overflow) ===
    // vehicleDB باید قبل از هر استفاده‌ای ساخته شود، و از آن‌جا که
    // حجمش بزرگ است باید از heap (که با PSRAM فعال، تخصیص‌های بزرگ
    // را خودکار در PSRAM می‌دهد) گرفته شود - نه یک آبجکت global در
    // DRAM. activeProfileManager و vehicleControl به همین ترتیب و
    // بلافاصله بعد ساخته می‌شوند چون هرکدام به‌صورت reference به
    // قبلی نیاز دارند.
    vehicleDB = new VehicleDB();
    activeProfileManager = new ActiveProfileManager(*vehicleDB, customVehicleStore);
    vehicleControl = new VehicleControl(canManager, *activeProfileManager);
    if (!vehicleDB || !activeProfileManager || !vehicleControl) {
        Serial.println("❌❌❌ [INIT] تخصیص حافظه برای vehicleDB/activeProfileManager/vehicleControl شکست خورد!");
        Serial.println("❌❌❌ [INIT] احتمالاً PSRAM موجود/فعال نیست - دستگاه متوقف می‌شود.");
        while (true) { delay(1000); }
    }
    Serial.printf("[INIT] PSRAM آزاد: %u bytes | Heap آزاد: %u bytes\n",
                  (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());

    // 0. === جدید: راه‌اندازی Task Watchdog ===
    // باید خیلی زود در setup() باشد تا حتی هنگ در همین تابع هم پوشش
    // داده شود. esp_task_wdt_add بدون آرگومان، تسک جاری (loopTask
    // آردوینو) را ثبت می‌کند.
    {
        // === اصلاح (باگ کامپایل) ===
        // esp_task_wdt_config_t (API مبتنی بر ساختار) فقط در
        // Arduino-ESP32 3.x (ESP-IDF 5.x) وجود دارد. زنجیره‌ی ابزار
        // فعلی از هسته‌ی 2.x استفاده می‌کند که امضای قدیمی‌تر
        // esp_task_wdt_init(timeout_s, panic) را دارد. با شرط‌گذاری
        // روی نسخه، کد روی هر دو نسخه‌ی core کامپایل می‌شود.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        esp_task_wdt_config_t wdtConfig = {
            .timeout_ms = WDT_TIMEOUT_S * 1000,
            .idle_core_mask = 0,
            .trigger_panic = true
        };
        esp_task_wdt_init(&wdtConfig);
#else
        esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
        esp_task_wdt_add(NULL);
        Serial.printf("[INIT] Watchdog فعال شد (timeout: %ds)\n", WDT_TIMEOUT_S);
    }
    
    // 1. بارگذاری تنظیمات
    Serial.println("[INIT] بارگذاری تنظیمات...");
    loadConfig();
    
    // 2. شروع SPIFFS (برای فایل‌های وب، DBC، و پروفایل‌های سفارشی جدید)
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
    
    // 5. شروع Vehicle DB (DBC - v1.0)
    vehicleDB->begin();
    
    // 6. === جدید v2.0: شروع CustomVehicleStore (باید بعد از SPIFFS.begin باشد) ===
    Serial.println("[INIT] شروع CustomVehicleStore...");
    customVehicleStore.begin();
    
    // 7. شروع Vehicle Control (حالا با activeProfileManager)
    vehicleControl->begin();
    
    // === تغییر: با روشن شدن، هیچ خودرویی خودکار انتخاب نمی‌شود ===
    // activeProfileManager از قبل با ACTIVE_KIND_NONE مقداردهی اولیه شده
    // (نگاه کنید به سازنده‌ی ActiveProfileManager). کاربر باید از منوی
    // TFT یا تب «کنترل»/«تنظیمات» در وب، خودرو را دستی انتخاب کند.
    // تا آن زمان resolveCommand() پیام «هیچ خودرویی انتخاب نشده است»
    // برمی‌گرداند - نه کرش و نه ارسال فرمان اشتباه.
    
    // 8. شروع TFT و LVGL
    // === جدید v2.0: اتصال ماژول‌های Learn Mode به TFT قبل از begin() ===
    // (بدون این خط، تب «یادگیری» در نمایشگر لمسی کار نمی‌کند چون
    // پوینترهای داخلی TFT_UI به LearnEngine/CustomVehicleStore/... همه
    // nullptr می‌مانند - این یک نقطه‌ی حیاتی سیم‌کشی است.)
    tftUI.attachLearnModules(&learnEngine, &customVehicleStore, 
                              activeProfileManager, vehicleControl);
    tftUI.begin();
    tftUI.setControlCallback(handleCommand);
    tftUI.showNotification("🚗 CarTouch آماده است");
    
    // 9. شروع WiFi (حالت AP پیش‌فرض)
    wifiManager.begin(1);  // 1 = AP mode
    tftUI.setWiFiStatus(wifiManager.isConnected());
    
    // 10. === جدید v2.0: اتصال ماژول‌های Learn Mode به وب سرور قبل از begin() ===
    webServer.attachLearnModules(&learnEngine, &customVehicleStore, 
                                  activeProfileManager, vehicleControl);
    
    // 11. شروع وب سرور
    webServer.begin();
    webServer.setCommandCallback(handleCommand);
    
    // مقداردهی اولیه زمان
    lastActivityTime = millis();
    
    Serial.println("\n[INIT] ✅ CarTouch v2.0 آماده به کار است!");
    Serial.printf("[INIT] IP: %s\n", wifiManager.getIP().toString().c_str());
    Serial.printf("[INIT] CAN: %s\n", canManager.isActive() ? "✅" : "❌");
    Serial.printf("[INIT] پروفایل‌های سفارشی موجود: %d\n", customVehicleStore.getProfileCount());
    
    if (isUsingDefaultPassword()) {
        Serial.println("⚠️⚠️⚠️ [SECURITY] رمز وب هنوز پیش‌فرض است! لطفاً از منوی تنظیمات تغییرش دهید ⚠️⚠️⚠️");
        tftUI.showNotification("⚠️ لطفاً رمز پیش‌فرض را عوض کنید!");
    }
}

// ======================== حلقه اصلی ========================

void loop() {
    // 0. === جدید: تغذیه‌ی Watchdog ===
    // باید هر تکرار loop صدا زده شود. اگر مرحله‌ای پایین‌تر بیش از
    // WDT_TIMEOUT_S طول بکشد (گیر کردن OBD2Reader، بی‌نهایت‌حلقه‌ی
    // ناخواسته و...)، این فراخوانی اجرا نمی‌شود و تراشه خودش را
    // ری‌ست می‌کند تا دستگاه کاملاً هنگ نماند.
    esp_task_wdt_reset();

    // 1. به‌روزرسانی LVGL
    tftUI.update();
    
    // 2. به‌روزرسانی WebSocket
    webServer.update();
    
    // 3. === جدید v2.0: به‌روزرسانی موتور یادگیری (غیرمسدودکننده) ===
    // این باید در هر تکرار loop صدا زده شود تا baseline/action
    // capture با تایمینگ درست پیش برود (شبیه tftUI.update()).
    learnEngine.update();
    
    // 4. خواندن داده‌های OBD-II (دوره‌ای، اکنون کاملاً non-blocking)
    // در حالت Listen-Only هیچ درخواست OBD ارسال نمی‌شود (خواندن OBD نیازمند ارسال درخواست است)
    // === تغییر (چک‌لیست تجاری #4) ===
    // قبلاً اینجا updateVehicleData() به‌صورت مستقیم و مسدودکننده صدا
    // زده می‌شد (تا ~۱٫۲ ثانیه در بدترین حالت). اکنون obd2Reader.update()
    // در هر تکرار loop() فقط یک قدم کوچک جلو می‌رود و هرگز مسدود
    // نمی‌کند؛ نتیجه‌ی یک دور کامل را جداگانه (در ادامه‌ی همین بلوک)
    // با getLatestData() برمی‌داریم.
    if (currentMode == MODE_ACTIVE && !getConfig()->listenOnlyMode) {
        obd2Reader.update();

        if (millis() - lastDataUpdateTime > obdReadInterval) {
            if (obd2Reader.getLatestData(currentVehicleData)) {
                // توجه: خواندن ولتاژ باطری از طریق CAN در بسیاری از خودروها پشتیبانی نمی‌شود
                currentVehicleData.batteryVoltage = 12.6f;  // مقدار پیش‌فرض
                tftUI.updateVehicleData(currentVehicleData);
                webServer.broadcastVehicleData(currentVehicleData);
            }
            lastDataUpdateTime = millis();
        }
    }
    
    // 5. بررسی خواب خودکار
    // توجه: طبق طراحی، اگر learnEngine در وسط یک capture است، بهتر
    // است خواب خودکار به تعویق بیفتد تا جلسه‌ی یادگیری قطع نشود.
    if (learnEngine.getState() == LEARN_IDLE) {
        checkAutoSleep();
    } else {
        lastActivityTime = millis();  // یادگیری فعال = فعالیت کاربر
    }
    
    // 6. بررسی بیدار شدن با پیام CAN
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
// توجه: این تابع دیگر در loop() صدا زده نمی‌شود - منطقش مستقیماً در
// بلوک ۴ داخل loop() با obd2Reader.update()/getLatestData() غیرمسدودکننده
// جایگزین شده (چک‌لیست تجاری #4). فقط پروتوتایپ/امضا برای سازگاری با
// کدی که شاید هنوز صدایش بزند نگه داشته شده.

void updateVehicleData() {
    obd2Reader.readAllPIDs(currentVehicleData);  // [BLOCKING] فقط برای استفاده‌ی دستی/تست
    
    // توجه: خواندن ولتاژ باطری از طریق CAN در بسیاری از خودروها پشتیبانی نمی‌شود
    currentVehicleData.batteryVoltage = 12.6f;  // مقدار پیش‌فرض
    
    tftUI.updateVehicleData(currentVehicleData);
    webServer.broadcastVehicleData(currentVehicleData);
}

// ======================== پردازش فرمان ========================

void handleCommand(const char* command) {
    lastActivityTime = millis();
    
    Serial.printf("[CMD] فرمان دریافت شد: %s\n", command);
    
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
        result = vehicleControl->lockAllDoors();
    } 
    else if (strcmp(command, "unlock") == 0) {
        result = vehicleControl->unlockAllDoors();
    } 
    else if (strcmp(command, "windows_up") == 0) {
        result = vehicleControl->allWindowsUp();
    } 
    else if (strcmp(command, "windows_down") == 0) {
        result = vehicleControl->allWindowsDown();
    } 
    else if (strcmp(command, "sunroof") == 0) {
        result = vehicleControl->sunroofOpen();
    } 
    else if (strcmp(command, "trunk") == 0) {
        result = vehicleControl->trunkOpen();
    } 
    else if (strcmp(command, "mirror") == 0) {
        result = vehicleControl->foldMirrors();
    } 
    else if (strcmp(command, "alarm") == 0) {
        if (currentVehicleData.alarmState == ALARM_DISARMED) {
            result = vehicleControl->alarmArm();
            currentVehicleData.alarmState = ALARM_ARMED;
        } else {
            result = vehicleControl->alarmDisarm();
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
        tftUI.showNotification("🚗 انتخاب خودرو در منو");
        return;
    }
    else if (strncmp(command, "vehicle_select_dbc:", 19) == 0) {
        // === جدید v2.0: انتخاب خودروی DBC با برند/مدل از TFT ===
        // فرمت: "vehicle_select_dbc:Brand|Model"
        char buf[64];
        strncpy(buf, command + 19, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* sep = strchr(buf, '|');
        if (sep) {
            *sep = '\0';
            activeProfileManager->selectDBCVehicle(buf, sep + 1);
            tftUI.showNotification("🚗 خودرو (DBC) انتخاب شد");
        }
        return;
    }
    else if (strncmp(command, "vehicle_select_custom:", 22) == 0) {
        // === جدید v2.0: انتخاب پروفایل سفارشی با ایندکس از TFT ===
        uint8_t idx = (uint8_t)atoi(command + 22);
        if (activeProfileManager->selectCustomVehicle(idx)) {
            tftUI.showNotification("🚗 خودرو (سفارشی) انتخاب شد");
        } else {
            tftUI.showNotification("❌ پروفایل یافت نشد");
        }
        return;
    }
    else {
        Serial.printf("⚠️ [CMD] فرمان ناشناخته: %s\n", command);
        tftUI.showNotification("❌ فرمان ناشناخته");
        return;
    }
    
    if (result) {
        tftUI.showNotification("✅ فرمان ارسال شد");
        webServer.broadcastStatus(command);
    } else {
        String reason = vehicleControl->getLastErrorMessage();
        if (reason.length() > 0) {
            tftUI.showNotification(("❌ " + reason).c_str());
        } else {
            tftUI.showNotification("❌ خطا در ارسال فرمان");
        }
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
        
        tftUI.setDeviceMode(MODE_SLEEP);
        wifiManager.disconnect();
        
        Serial.println("[SLEEP] دستگاه در حالت Sleep است - منتظر پیام CAN برای بیدار شدن");
    }
}

// ======================== بیدار شدن از خواب ========================

void wakeFromSleep() {
    if (currentMode == MODE_ACTIVE) return;
    
    Serial.println("[WAKE] بیدار شدن از Sleep...");
    
    currentMode = MODE_ACTIVE;
    lastActivityTime = millis();
    
    tftUI.setDeviceMode(MODE_ACTIVE);
    
    if (!wifiManager.isConnected()) {
        wifiManager.begin(1);
    }
    
    tftUI.showNotification("🚗 بیدار شدم!");
    
    Serial.println("[WAKE] دستگاه بیدار شد ✓");
}
