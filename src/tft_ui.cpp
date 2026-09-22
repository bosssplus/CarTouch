/**
 * tft_ui.cpp - پیاده‌سازی رابط کاربری لمسی با LVGL 8 (CarTouch v2.0)
 * 
 * === توجه مهم درباره‌ی الگوی این فایل ===
 * تمام صفحات جدید Learn Mode (Wizard، ورود دستی، تأیید) دقیقاً از
 * همان الگوی موجود در نسخه‌ی v1.0 برای `_passwordScreen` پیروی
 * می‌کنند: یک کانتینر تمام‌صفحه که پیش‌فرض HIDDEN است و با یک دکمه
 * باز/بسته می‌شود، و از همان کیبورد مجازی مشترک (`_keyboard`)
 * استفاده می‌کند.
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#include "tft_ui.h"
#include "learn_engine.h"
#include "custom_vehicle_store.h"
#include "active_profile_manager.h"
#include "vehicle_control.h"
#include <TFT_eSPI.h>

// ======================== متغیرهای static ========================

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSPI* pTft = &tft;

// === اصلاح (باگ کامپایل) ===
// این اشاره‌گر در نسخه‌ی v1.0 وجود داشت اما در تبدیل به v2.0 حذف شده
// بود، در حالی که تمام callback های استاتیک این فایل (لمس دکمه‌ها،
// صفحه‌ی رمز عبور، Learn Wizard و ...) به آن وابسته‌اند.
static TFT_UI* pThisUI = nullptr;

lv_disp_draw_buf_t TFT_UI::_dispBuf;
lv_color_t TFT_UI::_buf1[LVGL_BUF_SIZE];
lv_color_t TFT_UI::_buf2[LVGL_BUF_SIZE];

// === اصلاحیه (چک‌لیست تجاری #9) ===
// این دو آرایه‌ی ثابت قبلاً فقط یک حدس بودند و اصلاً در _lvglTouchRead
// (که به‌جایش یک map خطی ساده و ثابت با فرض نادرست "همیشه دقیقاً
// 320x240 خام" استفاده می‌کرد) خوانده نمی‌شدند. اکنون کالیبراسیون
// واقعی از AppConfig (که با runTouchCalibration() پر می‌شود) خوانده
// و مستقیماً به تابع calibrateTouch() خود TFT_eSPI داده می‌شود که
// خودش نگاشت خام→پیکسل صحیح (شامل چرخش صفحه) را انجام می‌دهد.

// ======================== تابع خواندن تاچ ========================
// === اصلاحیه (چک‌لیست تجاری #9) ===
// قبلاً: map(touchX, 0, 320, 0, TFT_WIDTH) - این فرض می‌کرد بازه‌ی
// خام ADC همیشه دقیقاً برابر ابعاد پیکسلی صفحه است که تقریباً هرگز
// درست نیست (کنترلر لمسی XPT2046 معمولاً بازه‌ی خام ADC ۰-۴۰۹۵ یا
// مشابه می‌دهد، نه ۰-۳۲۰) و هیچ نسبتی با کالیبراسیون واقعی صفحه
// نداشت. حالا اگر کالیبراسیون انجام شده باشد، از setTouch() خود
// TFT_eSPI (که یک‌بار در begin() با داده‌ی ذخیره‌شده صدا زده می‌شود)
// استفاده می‌شود که getTouch() را وادار می‌کند خودش مختصات صحیح
// پیکسل را برگرداند - دیگر نیازی به map دستی اینجا نیست.
void TFT_UI::_lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    uint16_t touchX, touchY;
    bool touched = pTft->getTouch(&touchX, &touchY, 600);
    
    if (touched) {
        // اگر setTouch() قبلاً با کالیبراسیون معتبر صدا زده شده باشد،
        // touchX/touchY از getTouch() همین‌الان مختصات پیکسل صحیح
        // هستند (TFT_eSPI خودش تبدیل را انجام می‌دهد) - نیازی به map
        // دستی نیست. اگر کالیبره نشده باشد (touchCalibrated=false)،
        // مقادیر خام برگردانده می‌شوند که نادرست خواهند بود؛ در آن
        // حالت main.cpp/setup() باید قبل از استفاده‌ی عادی از UI،
        // runTouchCalibration() را صدا زده باشد.
        data->point.x = touchX;
        data->point.y = touchY;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// === جدید v2.0: لیست برچسب‌های پیش‌فرض برای dropdown انتخاب فرمان ===
// این رشته با کاراکتر '\n' جداشده دقیقاً همان چیزی است که
// lv_dropdown_set_options انتظار دارد. index این آرایه با ایندکس
// انتخاب‌شده در dropdown مطابقت دارد.
static const char* LEARN_LABEL_OPTIONS[] = {
    CMD_LABEL_LOCK_ALL, CMD_LABEL_UNLOCK_ALL, CMD_LABEL_UNLOCK_DRIVER,
    CMD_LABEL_WINDOW_FL_UP, CMD_LABEL_WINDOW_FL_DOWN,
    CMD_LABEL_WINDOW_FR_UP, CMD_LABEL_WINDOW_FR_DOWN,
    CMD_LABEL_WINDOW_RL_UP, CMD_LABEL_WINDOW_RL_DOWN,
    CMD_LABEL_WINDOW_RR_UP, CMD_LABEL_WINDOW_RR_DOWN,
    CMD_LABEL_ALL_WINDOWS_UP, CMD_LABEL_ALL_WINDOWS_DOWN,
    CMD_LABEL_SUNROOF_OPEN, CMD_LABEL_SUNROOF_CLOSE, CMD_LABEL_SUNROOF_TILT,
    CMD_LABEL_TRUNK_OPEN, CMD_LABEL_TRUNK_LOCK,
    CMD_LABEL_MIRROR_FOLD, CMD_LABEL_MIRROR_UNFOLD,
    CMD_LABEL_ALARM_ARM, CMD_LABEL_ALARM_DISARM
};
static const char* LEARN_LABEL_DISPLAY_NAMES[] = {
    "قفل همه درب‌ها", "باز کردن همه درب‌ها", "باز کردن درب راننده",
    "شیشه جلو چپ بالا", "شیشه جلو چپ پایین",
    "شیشه جلو راست بالا", "شیشه جلو راست پایین",
    "شیشه عقب چپ بالا", "شیشه عقب چپ پایین",
    "شیشه عقب راست بالا", "شیشه عقب راست پایین",
    "همه شیشه‌ها بالا", "همه شیشه‌ها پایین",
    "باز کردن سانروف", "بستن سانروف", "کج کردن سانروف",
    "باز کردن صندوق", "قفل صندوق",
    "تا کردن آینه‌ها", "باز کردن آینه‌ها",
    "فعال کردن دزدگیر", "غیرفعال کردن دزدگیر"
};
#define LEARN_LABEL_COUNT (sizeof(LEARN_LABEL_OPTIONS) / sizeof(LEARN_LABEL_OPTIONS[0]))

// ======================== تابع Flush نمایشگر (v1.0) ========================

void TFT_UI::_lvglDisplayFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* colorMap) {
    uint32_t width = area->x2 - area->x1 + 1;
    uint32_t height = area->y2 - area->y1 + 1;
    
    pTft->startWrite();
    pTft->setAddrWindow(area->x1, area->y1, width, height);
    pTft->pushColors((uint16_t*)colorMap, width * height, true);
    pTft->endWrite();
    
    lv_disp_flush_ready(drv);
}

// ======================== سازنده ========================

TFT_UI::TFT_UI() {
    _initialized = false;
    _controlCallback = nullptr;
    _currentMode = MODE_ACTIVE;
    memset(&_vehicleData, 0, sizeof(VehicleData));
    
    _learnEngine = nullptr;
    _customStore = nullptr;
    _profileManager = nullptr;
    _vehicleControl = nullptr;
    
    _passwordScreen = nullptr;
    _passwordWarningLabel = nullptr;
    _taNewPass = nullptr;
    _taConfirmPass = nullptr;
    _passwordErrorLabel = nullptr;
    _keyboard = nullptr;
    
    _tabLearn = nullptr;
    _learnMainContainer = nullptr;
    _learnVehicleList = nullptr;
    _learnActiveVehicleLabel = nullptr;
    _learnWizardScreen = nullptr;
    _learnWizardTitle = nullptr;
    _learnWizardStatusLabel = nullptr;
    _learnWizardProgressBar = nullptr;
    _learnWizardCandidateList = nullptr;
    _learnWizardActionBtn = nullptr;
    _learnWizardCancelBtn = nullptr;
    _learnLabelDropdown = nullptr;
    _selectedCandidateIndex = -1;
    
    _manualEntryScreen = nullptr;
    _taManualCanId = nullptr;
    _taManualDataHex = nullptr;
    _manualLabelDropdown = nullptr;
    _manualExtendedCheckbox = nullptr;
    _manualErrorLabel = nullptr;
    
    _verifyScreen = nullptr;
    _verifyInfoLabel = nullptr;
    _verifyResultLabel = nullptr;
    _verifyProfileId = 0;
    _verifyLabel[0] = '\0';
    
    _selectedProfileForLearning = 0;
    
    pThisUI = this;
}

// ======================== اتصال ماژول‌های Learn Mode ========================

void TFT_UI::attachLearnModules(LearnEngine* learnEngine, CustomVehicleStore* customStore,
                                ActiveProfileManager* profileManager, VehicleControl* vehicleControl) {
    _learnEngine = learnEngine;
    _customStore = customStore;
    _profileManager = profileManager;
    _vehicleControl = vehicleControl;
}

// ======================== مقداردهی اولیه ========================

void TFT_UI::begin() {
    Serial.println("[TFT] مقداردهی اولیه صفحه نمایش...");
    
    tft.begin();
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(TFT_BLACK);
    
    pinMode(PIN_TFT_BL, OUTPUT);
    analogWrite(PIN_TFT_BL, TFT_BRIGHTNESS_DAY);

    // === جدید (چک‌لیست تجاری #9: کالیبراسیون واقعی تاچ‌اسکرین) ===
    // اگر قبلاً کالیبراسیون معتبری در NVS ذخیره شده، همان را به
    // TFT_eSPI بده تا getTouch() از همین الان مختصات پیکسل صحیح
    // برگرداند. اگر نه (اولین بوت دستگاه، یا کاربر از تنظیمات دوباره
    // درخواست کالیبراسیون داده)، runTouchCalibration() تعاملی را صدا
    // بزن - این یک بار، قبل از ساخته شدن رابط کاربری اصلی، مسدودکننده
    // اجرا می‌شود (چون بدون کالیبراسیون معتبر، خود دکمه‌های UI هم قابل
    // لمس دقیق نیستند - این تنها نقطه‌ای است که مسدود بودن قابل قبول
    // است).
    AppConfig* cfg = getConfig();
    if (cfg->touchCalibrated) {
        tft.setTouch(cfg->touchCalData);
        Serial.println("[TFT] کالیبراسیون تاچ ذخیره‌شده اعمال شد");
    } else {
        Serial.println("[TFT] کالیبراسیون تاچ یافت نشد - شروع ویزارد کالیبراسیون اولیه...");
        runTouchCalibration();
    }
    
    lv_init();
    
    lv_disp_draw_buf_init(&_dispBuf, _buf1, _buf2, LVGL_BUF_SIZE);
    
    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = TFT_WIDTH;
    dispDrv.ver_res = TFT_HEIGHT;
    dispDrv.flush_cb = _lvglDisplayFlush;
    dispDrv.draw_buf = &_dispBuf;
    lv_disp_drv_register(&dispDrv);
    
    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = _lvglTouchRead;
    lv_indev_drv_register(&indevDrv);
    
    // ساخت رابط کاربری - تب‌های v1.0 + تب جدید یادگیری
    _buildTabControl();
    _buildTabDashboard();
    _buildTabSettings();
    _buildTabLearn();               // === جدید v2.0 ===
    _buildPasswordScreen();
    _buildLearnWizardScreen();      // === جدید v2.0 ===
    _buildManualEntryScreen();      // === جدید v2.0 ===
    _buildVerifyScreen();           // === جدید v2.0 ===
    _refreshPasswordWarning();
    
    _initialized = true;
    Serial.println("[TFT] صفحه نمایش با موفقیت مقداردهی شد ✓");
}

// ======================== کالیبراسیون تاچ‌اسکرین ========================
// === جدید (چک‌لیست تجاری #9) ===
//
// از calibrateTouch() خودِ کتابخانه‌ی TFT_eSPI استفاده می‌کند که یک
// روال استاندارد و آزموده‌شده‌ی صنعتی است: ۴ گوشه + مرکز صفحه یک‌به‌یک
// نمایش داده می‌شوند، کاربر هرکدام را لمس می‌کند، و کتابخانه خودش
// ماتریس تبدیل خام→پیکسل را (با احتساب چرخش TFT_ROTATION فعلی) در
// calData[5] محاسبه می‌کند. این عدد سپس در NVS ذخیره می‌شود تا در
// بوت‌های بعدی نیازی به تکرار کالیبراسیون نباشد.
void TFT_UI::runTouchCalibration() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 20);
    tft.println("کالیبراسیون لمسی");
    tft.setTextSize(1);
    tft.setCursor(20, 60);
    tft.println("چهار گوشه صفحه که چشمک می‌زنند را لمس کنید");

    uint16_t calData[5];
    // پارامترها: رنگ کراس‌هیر، رنگ پس‌زمینه، timeout به میلی‌ثانیه (۱۵
    // ثانیه در هر نقطه - برای کاربری که در ماشین نشسته و ممکن است
    // دستش پر باشد، زمان کافی).
    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15000);

    AppConfig* cfg = getConfig();
    memcpy(cfg->touchCalData, calData, sizeof(calData));
    cfg->touchCalibrated = true;
    saveConfig();

    tft.setTouch(calData);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(20, 20);
    tft.println("کالیبراسیون ذخیره شد ✓");
    delay(1000);  // فقط یک‌بار در بوت/تنظیمات اجرا می‌شود - مسدود بودن اینجا بی‌اثر است

    Serial.printf("[TFT] کالیبراسیون ذخیره شد: {%u,%u,%u,%u,%u}\n",
                  calData[0], calData[1], calData[2], calData[3], calData[4]);
}

// ======================== به‌روزرسانی ========================

void TFT_UI::update() {
    if (!_initialized) return;
    lv_timer_handler();
    
    static uint32_t lastPasswordCheck = 0;
    if (millis() - lastPasswordCheck > 5000) {
        _refreshPasswordWarning();
        lastPasswordCheck = millis();
    }
    
    // === جدید v2.0: اگر ویزارد یادگیری باز است، وضعیتش را دوره‌ای
    // به‌روز کن (چون baseline/action capture زمان‌بر و ناهمگام است)
    if (_learnWizardScreen && !lv_obj_has_flag(_learnWizardScreen, LV_OBJ_FLAG_HIDDEN)) {
        static uint32_t lastWizardRefresh = 0;
        if (millis() - lastWizardRefresh > 200) {
            _refreshLearnWizardUI();
            lastWizardRefresh = millis();
        }
    }
}

// ======================== تنظیم callback ========================

void TFT_UI::setControlCallback(UIControlCallback cb) {
    _controlCallback = cb;
}

// ======================== ساختن تب کنترل (v1.0 - بدون تغییر) ========================

void TFT_UI::_buildTabControl() {
    _tabView = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 35);
    
    _tabControl = lv_tabview_add_tab(_tabView, "کنترل");
    _tabDashboard = lv_tabview_add_tab(_tabView, "داشبورد");
    _tabSettings = lv_tabview_add_tab(_tabView, "تنظیمات");
    // توجه: تب "یادگیری" در _buildTabLearn() اضافه می‌شود (بعد از این سه تب فراخوانی می‌شود)
    
    lv_obj_t* btnLock = lv_btn_create(_tabControl);
    lv_obj_set_size(btnLock, 100, 45);
    lv_obj_set_pos(btnLock, 10, 10);
    lv_obj_add_event_cb(btnLock, _btnLockEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelLock = lv_label_create(btnLock);
    lv_label_set_text(labelLock, "🔒 قفل");
    lv_obj_center(labelLock);
    
    lv_obj_t* btnUnlock = lv_btn_create(_tabControl);
    lv_obj_set_size(btnUnlock, 100, 45);
    lv_obj_set_pos(btnUnlock, 130, 10);
    lv_obj_add_event_cb(btnUnlock, _btnUnlockEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelUnlock = lv_label_create(btnUnlock);
    lv_label_set_text(labelUnlock, "🔓 باز");
    lv_obj_center(labelUnlock);
    
    lv_obj_t* lblWindow = lv_label_create(_tabControl);
    lv_label_set_text(lblWindow, "شیشه‌ها:");
    lv_obj_set_pos(lblWindow, 10, 65);
    
    lv_obj_t* btnWinUp = lv_btn_create(_tabControl);
    lv_obj_set_size(btnWinUp, 100, 40);
    lv_obj_set_pos(btnWinUp, 10, 85);
    lv_obj_add_event_cb(btnWinUp, _btnWindowUpEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelWinUp = lv_label_create(btnWinUp);
    lv_label_set_text(labelWinUp, "⬆ بالا");
    lv_obj_center(labelWinUp);
    
    lv_obj_t* btnWinDown = lv_btn_create(_tabControl);
    lv_obj_set_size(btnWinDown, 100, 40);
    lv_obj_set_pos(btnWinDown, 130, 85);
    lv_obj_add_event_cb(btnWinDown, _btnWindowDownEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelWinDown = lv_label_create(btnWinDown);
    lv_label_set_text(labelWinDown, "⬇ پایین");
    lv_obj_center(labelWinDown);
    
    lv_obj_t* btnSunroof = lv_btn_create(_tabControl);
    lv_obj_set_size(btnSunroof, 100, 40);
    lv_obj_set_pos(btnSunroof, 10, 140);
    lv_obj_add_event_cb(btnSunroof, _btnSunroofEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelSunroof = lv_label_create(btnSunroof);
    lv_label_set_text(labelSunroof, "☀ سانروف");
    lv_obj_center(labelSunroof);
    
    lv_obj_t* btnTrunk = lv_btn_create(_tabControl);
    lv_obj_set_size(btnTrunk, 100, 40);
    lv_obj_set_pos(btnTrunk, 130, 140);
    lv_obj_add_event_cb(btnTrunk, _btnTrunkEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelTrunk = lv_label_create(btnTrunk);
    lv_label_set_text(labelTrunk, "🔙 صندوق");
    lv_obj_center(labelTrunk);
    
    lv_obj_t* btnMirror = lv_btn_create(_tabControl);
    lv_obj_set_size(btnMirror, 100, 40);
    lv_obj_set_pos(btnMirror, 10, 195);
    lv_obj_add_event_cb(btnMirror, _btnMirrorEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelMirror = lv_label_create(btnMirror);
    lv_label_set_text(labelMirror, "🪞 آینه");
    lv_obj_center(labelMirror);
    
    lv_obj_t* btnAlarm = lv_btn_create(_tabControl);
    lv_obj_set_size(btnAlarm, 100, 40);
    lv_obj_set_pos(btnAlarm, 130, 195);
    lv_obj_add_event_cb(btnAlarm, _btnAlarmEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelAlarm = lv_label_create(btnAlarm);
    lv_label_set_text(labelAlarm, "🚨 دزدگیر");
    lv_obj_center(labelAlarm);
    
    lv_obj_t* btnListenOnly = lv_btn_create(_tabControl);
    lv_obj_set_size(btnListenOnly, 220, 40);
    lv_obj_set_pos(btnListenOnly, 10, 250);
    lv_obj_add_event_cb(btnListenOnly, _btnListenOnlyEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelListenOnly = lv_label_create(btnListenOnly);
    lv_label_set_text(labelListenOnly, "👂 حالت Listen-Only");
    lv_obj_center(labelListenOnly);
    
    _statusCAN = lv_label_create(_tabControl);
    lv_obj_set_pos(_statusCAN, 10, 300);
    lv_label_set_text(_statusCAN, "CAN: ?");
    
    _statusWiFi = lv_label_create(_tabControl);
    lv_obj_set_pos(_statusWiFi, 120, 300);
    lv_label_set_text(_statusWiFi, "WiFi: ?");
}

// ======================== ساختن تب Dashboard (v1.0 - بدون تغییر) ========================

void TFT_UI::_buildTabDashboard() {
    if (!_tabDashboard) return;
    
    _labelSpeed = lv_label_create(_tabDashboard);
    lv_obj_set_pos(_labelSpeed, 10, 10);
    lv_label_set_text(_labelSpeed, "۰ km/h");
    lv_obj_set_style_text_font(_labelSpeed, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(_labelSpeed, lv_color_hex(0x00FF00), 0);
    
    _labelRPM = lv_label_create(_tabDashboard);
    lv_obj_set_pos(_labelRPM, 10, 70);
    lv_label_set_text(_labelRPM, "RPM: ۰");
    lv_obj_set_style_text_font(_labelRPM, &lv_font_montserrat_20, 0);
    
    _labelTemp = lv_label_create(_tabDashboard);
    lv_obj_set_pos(_labelTemp, 10, 100);
    lv_label_set_text(_labelTemp, "🌡 دمای موتور: -- °C");
    
    _labelVolt = lv_label_create(_tabDashboard);
    lv_obj_set_pos(_labelVolt, 10, 130);
    lv_label_set_text(_labelVolt, "🔋 ولتاژ: -- V");
    
    _labelFuel = lv_label_create(_tabDashboard);
    lv_obj_set_pos(_labelFuel, 10, 160);
    lv_label_set_text(_labelFuel, "⛽ سوخت: --%");
    
    lv_obj_t* labelDoorStatus = lv_label_create(_tabDashboard);
    lv_obj_set_pos(labelDoorStatus, 10, 200);
    lv_label_set_text(labelDoorStatus, "🚪 درب‌ها: --");
}

// ======================== ساختن تب تنظیمات (v1.0 - بدون تغییر) ========================

void TFT_UI::_buildTabSettings() {
    if (!_tabSettings) return;
    
    lv_obj_t* labelTitle = lv_label_create(_tabSettings);
    lv_obj_set_pos(labelTitle, 10, 10);
    lv_label_set_text(labelTitle, "تنظیمات");
    lv_obj_set_style_text_font(labelTitle, &lv_font_montserrat_20, 0);
    
    _passwordWarningLabel = lv_label_create(_tabSettings);
    lv_obj_set_pos(_passwordWarningLabel, 10, 40);
    lv_label_set_text(_passwordWarningLabel, "⚠️ رمز پیش‌فرض فعال است - تغییر دهید");
    lv_obj_set_style_text_color(_passwordWarningLabel, lv_color_hex(0xFF4444), 0);
    lv_obj_add_flag(_passwordWarningLabel, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_t* btnVehicle = lv_btn_create(_tabSettings);
    lv_obj_set_size(btnVehicle, 220, 45);
    lv_obj_set_pos(btnVehicle, 10, 65);
    lv_obj_add_event_cb(btnVehicle, _btnVehicleSelectEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelVehicle = lv_label_create(btnVehicle);
    lv_label_set_text(labelVehicle, "🚗 انتخاب خودرو");
    lv_obj_center(labelVehicle);
    
    lv_obj_t* btnTheme = lv_btn_create(_tabSettings);
    lv_obj_set_size(btnTheme, 220, 45);
    lv_obj_set_pos(btnTheme, 10, 120);
    lv_obj_add_event_cb(btnTheme, _btnThemeEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelTheme = lv_label_create(btnTheme);
    lv_label_set_text(labelTheme, "🌙 حالت شب/روز");
    lv_obj_center(labelTheme);
    
    lv_obj_t* btnPassword = lv_btn_create(_tabSettings);
    lv_obj_set_size(btnPassword, 220, 45);
    lv_obj_set_pos(btnPassword, 10, 175);
    lv_obj_set_style_bg_color(btnPassword, lv_color_hex(0xB33A3A), 0);
    lv_obj_add_event_cb(btnPassword, _btnChangePasswordEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelPassword = lv_label_create(btnPassword);
    lv_label_set_text(labelPassword, "🔐 تغییر رمز ورود");
    lv_obj_center(labelPassword);

    // === جدید (چک‌لیست تجاری #9: کالیبراسیون واقعی تاچ‌اسکرین) ===
    // به کاربر اجازه می‌دهد در هر زمان (نه فقط اولین بوت) دوباره
    // کالیبراسیون را اجرا کند - مثلاً اگر صفحه عوض شد یا دقت لمس کم
    // شده باشد.
    lv_obj_t* btnRecalibrate = lv_btn_create(_tabSettings);
    lv_obj_set_size(btnRecalibrate, 220, 45);
    lv_obj_set_pos(btnRecalibrate, 10, 230);
    lv_obj_set_style_bg_color(btnRecalibrate, lv_color_hex(0x3A6EA5), 0);
    lv_obj_add_event_cb(btnRecalibrate, _btnRecalibrateTouchEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelRecalibrate = lv_label_create(btnRecalibrate);
    lv_label_set_text(labelRecalibrate, "🎯 کالیبراسیون مجدد لمس");
    lv_obj_center(labelRecalibrate);
    
    lv_obj_t* labelInfo = lv_label_create(_tabSettings);
    lv_obj_set_pos(labelInfo, 10, 285);
    lv_label_set_text(labelInfo, 
        "⚠ خودرو باید خاموش باشد\n"
        "قبل از نصب باتری را جدا کنید\n"
        "CarTouch v2.0");
}

// ======================== جدید v2.0: ساختن تب یادگیری ========================

void TFT_UI::_buildTabLearn() {
    if (!_tabView) return;
    
    _tabLearn = lv_tabview_add_tab(_tabView, "یادگیری");
    if (!_tabLearn) return;
    
    lv_obj_t* title = lv_label_create(_tabLearn);
    lv_obj_set_pos(title, 10, 5);
    lv_label_set_text(title, "🎓 یادگیری فرمان از خودرو");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    
    _learnActiveVehicleLabel = lv_label_create(_tabLearn);
    lv_obj_set_pos(_learnActiveVehicleLabel, 10, 35);
    lv_label_set_text(_learnActiveVehicleLabel, "خودروی فعال: —");
    lv_obj_set_style_text_color(_learnActiveVehicleLabel, lv_color_hex(0x00D4FF), 0);
    
    // دکمه شروع یادگیری جدید
    lv_obj_t* btnStart = lv_btn_create(_tabLearn);
    lv_obj_set_size(btnStart, 105, 40);
    lv_obj_set_pos(btnStart, 10, 60);
    lv_obj_set_style_bg_color(btnStart, lv_color_hex(0x2ECC71), 0);
    lv_obj_add_event_cb(btnStart, _btnStartLearnEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelStart = lv_label_create(btnStart);
    lv_label_set_text(labelStart, "🎓 یادگیری جدید");
    lv_obj_center(labelStart);
    
    // دکمه ورود دستی
    lv_obj_t* btnManual = lv_btn_create(_tabLearn);
    lv_obj_set_size(btnManual, 105, 40);
    lv_obj_set_pos(btnManual, 125, 60);
    lv_obj_set_style_bg_color(btnManual, lv_color_hex(0x3498DB), 0);
    lv_obj_add_event_cb(btnManual, _btnManualEntryEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelManual = lv_label_create(btnManual);
    lv_label_set_text(labelManual, "✍️ ورود دستی");
    lv_obj_center(labelManual);
    
    lv_obj_t* listTitle = lv_label_create(_tabLearn);
    lv_obj_set_pos(listTitle, 10, 108);
    lv_label_set_text(listTitle, "پروفایل‌های من:");
    lv_obj_set_style_text_color(listTitle, lv_color_hex(0x888888), 0);
    
    // لیست پروفایل‌های سفارشی (هر ردیف قابل‌کلیک برای انتخاب به‌عنوان فعال)
    _learnVehicleList = lv_list_create(_tabLearn);
    lv_obj_set_size(_learnVehicleList, 220, 190);
    lv_obj_set_pos(_learnVehicleList, 10, 130);
    
    _refreshLearnVehicleList();
}

// ======================== به‌روزرسانی لیست پروفایل‌های سفارشی ========================

void TFT_UI::_refreshLearnVehicleList() {
    if (!_learnVehicleList || !_customStore) return;
    
    lv_obj_clean(_learnVehicleList);
    
    bool anyFound = false;
    for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
        CustomVehicleProfile summary;
        if (!_customStore->getProfileSummary(i, summary)) continue;
        anyFound = true;
        
        char buf[64];
        snprintf(buf, sizeof(buf), "%s (%d فرمان)", summary.name, summary.commandCount);
        
        lv_obj_t* btn = lv_list_add_btn(_learnVehicleList, LV_SYMBOL_DRIVE, buf);
        // ایندکس پروفایل را در user_data ذخیره می‌کنیم تا در event handler قابل بازیابی باشد
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, _vehicleListItemEventHandler, LV_EVENT_CLICKED, NULL);
    }
    
    if (!anyFound) {
        lv_obj_t* emptyLabel = lv_label_create(_learnVehicleList);
        lv_label_set_text(emptyLabel, "هنوز پروفایلی ساخته نشده.\nاز «یادگیری جدید» یا\n«ورود دستی» شروع کنید.");
    }
    
    // به‌روزرسانی برچسب خودروی فعال
    if (_learnActiveVehicleLabel && _profileManager) {
        char nameBuf[48];
        _profileManager->getActiveVehicleName(nameBuf, sizeof(nameBuf));
        char full[64];
        snprintf(full, sizeof(full), "خودروی فعال: %s", nameBuf);
        lv_label_set_text(_learnActiveVehicleLabel, full);
    }
}

// ======================== انتخاب یک پروفایل از لیست ========================

void TFT_UI::_vehicleListItemEventHandler(lv_event_t* e) {
    if (!pThisUI || !pThisUI->_profileManager) return;
    
    lv_obj_t* btn = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(btn);
    
    if (pThisUI->_profileManager->selectCustomVehicle((uint8_t)index)) {
        pThisUI->showNotification("🚗 این خودرو به‌عنوان فعال انتخاب شد");
        pThisUI->_refreshLearnVehicleList();
    } else {
        pThisUI->showNotification("❌ خطا در انتخاب پروفایل");
    }
}

// ======================================================================
// === جدید v2.0: مودال Wizard یادگیری ===
// این مودال دقیقاً همان الگوی _passwordScreen v1.0 را دنبال می‌کند:
// یک کانتینر تمام‌صفحه که پیش‌فرض HIDDEN است.
// ======================================================================

void TFT_UI::_buildLearnWizardScreen() {
    _learnWizardScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_learnWizardScreen, TFT_WIDTH, TFT_HEIGHT);
    lv_obj_set_pos(_learnWizardScreen, 0, 0);
    lv_obj_set_style_bg_color(_learnWizardScreen, lv_color_hex(0x0F1A30), 0);
    lv_obj_set_style_bg_opa(_learnWizardScreen, LV_OPA_COVER, 0);
    lv_obj_add_flag(_learnWizardScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_learnWizardScreen, LV_OBJ_FLAG_SCROLLABLE);
    
    _learnWizardTitle = lv_label_create(_learnWizardScreen);
    lv_obj_set_pos(_learnWizardTitle, 10, 8);
    lv_label_set_text(_learnWizardTitle, "🎓 یادگیری فرمان");
    lv_obj_set_style_text_font(_learnWizardTitle, &lv_font_montserrat_20, 0);
    
    // انتخاب برچسب فرمان (فقط در حالت IDLE قابل تغییر)
    lv_obj_t* lblChoose = lv_label_create(_learnWizardScreen);
    lv_obj_set_pos(lblChoose, 10, 38);
    lv_label_set_text(lblChoose, "فرمان مورد نظر:");
    
    _learnLabelDropdown = lv_dropdown_create(_learnWizardScreen);
    lv_obj_set_size(_learnLabelDropdown, 220, 35);
    lv_obj_set_pos(_learnLabelDropdown, 10, 58);
    {
        // ساخت رشته‌ی گزینه‌ها با نام‌های نمایشی فارسی
        String options = "";
        for (int i = 0; i < (int)LEARN_LABEL_COUNT; i++) {
            options += LEARN_LABEL_DISPLAY_NAMES[i];
            if (i < (int)LEARN_LABEL_COUNT - 1) options += "\n";
        }
        lv_dropdown_set_options(_learnLabelDropdown, options.c_str());
    }
    
    // متن وضعیت (پیام‌های راهنمای هر مرحله - بخش ۹ سند)
    _learnWizardStatusLabel = lv_label_create(_learnWizardScreen);
    lv_obj_set_pos(_learnWizardStatusLabel, 10, 100);
    lv_obj_set_width(_learnWizardStatusLabel, 220);
    lv_label_set_long_mode(_learnWizardStatusLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_learnWizardStatusLabel, 
        "در این حالت، دستگاه فقط پیام‌های CAN Bus را می‌شنود و هیچ "
        "فرمانی ارسال نمی‌کند.");
    
    // نوار پیشرفت (فقط حین baseline/action capture دیده می‌شود)
    _learnWizardProgressBar = lv_bar_create(_learnWizardScreen);
    lv_obj_set_size(_learnWizardProgressBar, 220, 15);
    lv_obj_set_pos(_learnWizardProgressBar, 10, 160);
    lv_bar_set_range(_learnWizardProgressBar, 0, 100);
    lv_obj_add_flag(_learnWizardProgressBar, LV_OBJ_FLAG_HIDDEN);
    
    // لیست کاندیدها (فقط در حالت CANDIDATES_READY دیده می‌شود)
    _learnWizardCandidateList = lv_list_create(_learnWizardScreen);
    lv_obj_set_size(_learnWizardCandidateList, 220, 120);
    lv_obj_set_pos(_learnWizardCandidateList, 10, 100);
    lv_obj_add_flag(_learnWizardCandidateList, LV_OBJ_FLAG_HIDDEN);
    
    // دکمه‌ی اقدام اصلی (متنش بر اساس state تغییر می‌کند)
    _learnWizardActionBtn = lv_btn_create(_learnWizardScreen);
    lv_obj_set_size(_learnWizardActionBtn, 220, 45);
    lv_obj_set_pos(_learnWizardActionBtn, 10, 225);
    lv_obj_set_style_bg_color(_learnWizardActionBtn, lv_color_hex(0x2ECC71), 0);
    lv_obj_add_event_cb(_learnWizardActionBtn, _btnLearnWizardActionEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* actionLabel = lv_label_create(_learnWizardActionBtn);
    lv_label_set_text(actionLabel, "▶ شروع ضبط پیش‌زمینه");
    lv_obj_center(actionLabel);
    
    _learnWizardCancelBtn = lv_btn_create(_learnWizardScreen);
    lv_obj_set_size(_learnWizardCancelBtn, 220, 40);
    lv_obj_set_pos(_learnWizardCancelBtn, 10, 275);
    lv_obj_set_style_bg_color(_learnWizardCancelBtn, lv_color_hex(0x555555), 0);
    lv_obj_add_event_cb(_learnWizardCancelBtn, _btnLearnWizardCancelEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cancelLabel = lv_label_create(_learnWizardCancelBtn);
    lv_label_set_text(cancelLabel, "❌ انصراف");
    lv_obj_center(cancelLabel);
}

void TFT_UI::_openLearnWizard() {
    if (!_learnWizardScreen) return;
    _selectedCandidateIndex = -1;
    lv_obj_clear_flag(_learnWizardScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_learnWizardScreen);
    _refreshLearnWizardUI();
}

void TFT_UI::_closeLearnWizard() {
    if (!_learnWizardScreen) return;
    lv_obj_add_flag(_learnWizardScreen, LV_OBJ_FLAG_HIDDEN);
    if (_learnEngine) _learnEngine->cancel();
}

// ======================== به‌روزرسانی UI ویزارد بر اساس state ========================

void TFT_UI::_refreshLearnWizardUI() {
    if (!_learnEngine || !_learnWizardActionBtn) return;
    
    LearnModeState state = _learnEngine->getState();
    lv_obj_t* actionLabel = lv_obj_get_child(_learnWizardActionBtn, 0);
    
    switch (state) {
        case LEARN_IDLE:
            lv_obj_clear_flag(_learnLabelDropdown, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_learnWizardProgressBar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_learnWizardCandidateList, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(_learnWizardStatusLabel,
                "در این حالت، دستگاه فقط پیام‌های CAN Bus را می‌شنود و "
                "هیچ فرمانی ارسال نمی‌کند. فرمان مورد نظر را از لیست "
                "بالا انتخاب و «شروع» را بزنید.");
            if (actionLabel) lv_label_set_text(actionLabel, "▶ شروع ضبط پیش‌زمینه");
            break;
            
        case LEARN_BASELINE_CAPTURE:
            lv_obj_add_flag(_learnLabelDropdown, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(_learnWizardProgressBar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_learnWizardCandidateList, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(_learnWizardProgressBar, _learnEngine->getProgressPercent(), LV_ANIM_OFF);
            lv_label_set_text(_learnWizardStatusLabel,
                "⏺ در حال ضبط پیش‌زمینه... لطفاً هنوز دکمه فیزیکی خودرو "
                "را نزنید.");
            if (actionLabel) lv_label_set_text(actionLabel, "⏳ در حال ضبط...");
            break;
            
        case LEARN_WAITING_ACTION:
            lv_obj_add_flag(_learnWizardProgressBar, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(_learnWizardStatusLabel,
                "✅ ضبط پیش‌زمینه تمام شد. حالا آماده باشید و وقتی دکمه "
                "«شروع ضبط اقدام» را زدید، بلافاصله دکمه فیزیکی خودرو "
                "را فشار دهید.");
            if (actionLabel) lv_label_set_text(actionLabel, "▶ شروع ضبط اقدام");
            break;
            
        case LEARN_ACTION_CAPTURE:
            lv_obj_clear_flag(_learnWizardProgressBar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(_learnWizardProgressBar, _learnEngine->getProgressPercent(), LV_ANIM_OFF);
            lv_label_set_text(_learnWizardStatusLabel,
                "🔴 همین الان دکمه فیزیکی خودرو را بزنید!");
            if (actionLabel) lv_label_set_text(actionLabel, "⏳ در حال ضبط...");
            break;
            
        case LEARN_CANDIDATES_READY: {
            lv_obj_add_flag(_learnWizardProgressBar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(_learnWizardCandidateList, LV_OBJ_FLAG_HIDDEN);
            
            uint8_t count = _learnEngine->getCandidateCount();
            char statusBuf[96];
            snprintf(statusBuf, sizeof(statusBuf), 
                     "%d کاندید یافت شد. یکی را انتخاب کنید که با فشردن "
                     "دکمه مطابقت داشت:", count);
            lv_label_set_text(_learnWizardStatusLabel, statusBuf);
            
            // پر کردن لیست کاندیدها (فقط یک‌بار، وقتی تازه وارد این state شدیم)
            static uint8_t lastRenderedCount = 255;
            if (lastRenderedCount != count) {
                lv_obj_clean(_learnWizardCandidateList);
                for (int i = 0; i < count; i++) {
                    LearnCandidate c;
                    if (!_learnEngine->getCandidate(i, c)) continue;
                    
                    char buf[64];
                    snprintf(buf, sizeof(buf), "0x%03X %s (x%d)", 
                             c.canId, c.isNewMessage ? "[جدید]" : "[تغییر]", 
                             c.seenCountInAction);
                    lv_obj_t* btn = lv_list_add_btn(_learnWizardCandidateList, LV_SYMBOL_LIST, buf);
                    lv_obj_set_user_data(btn, (void*)(intptr_t)i);
                    lv_obj_add_event_cb(btn, _learnCandidateSelectEventHandler, LV_EVENT_CLICKED, NULL);
                }
                lastRenderedCount = count;
            }
            
            if (actionLabel) lv_label_set_text(actionLabel, 
                _selectedCandidateIndex >= 0 ? "💾 ذخیره کاندید انتخاب‌شده" : "⬆ یک کاندید را انتخاب کنید");
            break;
        }
        
        case LEARN_ERROR:
            lv_label_set_text(_learnWizardStatusLabel, "⚠️ خطایی رخ داد. دوباره تلاش کنید.");
            break;
    }
}

// ======================== دکمه‌ی اقدام اصلی ویزارد ========================

void TFT_UI::_onLearnWizardActionPressed() {
    if (!_learnEngine) return;
    
    LearnModeState state = _learnEngine->getState();
    
    if (state == LEARN_IDLE) {
        // خواندن برچسب انتخاب‌شده از dropdown و شروع
        uint16_t selectedIdx = lv_dropdown_get_selected(_learnLabelDropdown);
        if (selectedIdx >= LEARN_LABEL_COUNT) return;
        
        _learnEngine->beginLearning(LEARN_LABEL_OPTIONS[selectedIdx], LEARN_LABEL_DISPLAY_NAMES[selectedIdx]);
        _learnEngine->startBaselineCapture();
        
    } else if (state == LEARN_WAITING_ACTION) {
        _learnEngine->confirmReadyForAction();
        
    } else if (state == LEARN_CANDIDATES_READY) {
        if (_selectedCandidateIndex >= 0) {
            _saveLearnedCommand();
        } else {
            showNotification("⚠️ ابتدا یک کاندید را از لیست انتخاب کنید");
        }
    }
    
    _refreshLearnWizardUI();
}

// ======================== انتخاب یک کاندید از لیست ========================

void TFT_UI::_onLearnCandidateSelected(int index) {
    _selectedCandidateIndex = index;
    showNotification("✅ کاندید انتخاب شد - حالا «ذخیره» را بزنید");
    _refreshLearnWizardUI();
}

void TFT_UI::_learnCandidateSelectEventHandler(lv_event_t* e) {
    if (!pThisUI) return;
    lv_obj_t* btn = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(btn);
    pThisUI->_onLearnCandidateSelected(index);
}

// ======================== ذخیره‌ی فرمان یادگرفته‌شده ========================

void TFT_UI::_saveLearnedCommand() {
    if (!_learnEngine || !_customStore || _selectedCandidateIndex < 0) return;
    
    LearnCandidate candidate;
    if (!_learnEngine->getCandidate((uint8_t)_selectedCandidateIndex, candidate)) {
        showNotification("❌ خطا در دریافت کاندید");
        return;
    }
    
    LearnedCommand cmd;
    strncpy(cmd.label, _learnEngine->getCurrentLabel(), sizeof(cmd.label) - 1);
    strncpy(cmd.displayName, _learnEngine->getCurrentDisplayName(), sizeof(cmd.displayName) - 1);
    cmd.canId = candidate.canId;
    cmd.isExtended = candidate.isExtended;
    cmd.length = candidate.length;
    memcpy(cmd.data, candidate.data, candidate.length);
    cmd.source = SOURCE_LEARNED;
    cmd.status = CMD_UNVERIFIED;  // طبق بخش ۴.۲ سند - همیشه ابتدا تأییدنشده
    cmd.timesObserved = candidate.seenCountInAction;
    cmd.failCount = 0;
    cmd.createdAt = millis();
    
    // اگر هنوز هیچ پروفایلی برای این جلسه انتخاب نشده، یکی می‌سازیم
    // (در نسخه‌ی TFT برای سادگی همیشه به پروفایل فعلی _selectedProfileForLearning اضافه می‌کنیم؛
    // اگر پروفایلی وجود نداشته باشد از طریق وب باید ابتدا ساخته شود -
    // این یک محدودیت شناخته‌شده‌ی رابط TFT است، طبق بخش ۱۲ سند قابل بهبود).
    bool ok = _customStore->upsertCommand(_selectedProfileForLearning, cmd);
    
    if (ok) {
        showNotification("✅ فرمان ذخیره شد (هنوز تأییدنشده - از لیست تأیید کنید)");
        _learnEngine->cancel();
        _closeLearnWizard();
        _refreshLearnVehicleList();
    } else {
        showNotification("❌ ذخیره ناموفق بود - آیا پروفایلی انتخاب شده؟");
    }
}

// ======================================================================
// === جدید v2.0: صفحه‌ی ورود دستی (بخش ۵ سند) ===
// ======================================================================

void TFT_UI::_buildManualEntryScreen() {
    _manualEntryScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_manualEntryScreen, TFT_WIDTH, TFT_HEIGHT);
    lv_obj_set_pos(_manualEntryScreen, 0, 0);
    lv_obj_set_style_bg_color(_manualEntryScreen, lv_color_hex(0x0F1A30), 0);
    lv_obj_set_style_bg_opa(_manualEntryScreen, LV_OPA_COVER, 0);
    lv_obj_add_flag(_manualEntryScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_manualEntryScreen, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t* title = lv_label_create(_manualEntryScreen);
    lv_obj_set_pos(title, 10, 8);
    lv_label_set_text(title, "✍️ ورود دستی فرمان");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    
    lv_obj_t* lblChoose = lv_label_create(_manualEntryScreen);
    lv_obj_set_pos(lblChoose, 10, 38);
    lv_label_set_text(lblChoose, "فرمان:");
    
    _manualLabelDropdown = lv_dropdown_create(_manualEntryScreen);
    lv_obj_set_size(_manualLabelDropdown, 220, 32);
    lv_obj_set_pos(_manualLabelDropdown, 10, 56);
    {
        String options = "";
        for (int i = 0; i < (int)LEARN_LABEL_COUNT; i++) {
            options += LEARN_LABEL_DISPLAY_NAMES[i];
            if (i < (int)LEARN_LABEL_COUNT - 1) options += "\n";
        }
        lv_dropdown_set_options(_manualLabelDropdown, options.c_str());
    }
    
    lv_obj_t* lblCanId = lv_label_create(_manualEntryScreen);
    lv_obj_set_pos(lblCanId, 10, 96);
    lv_label_set_text(lblCanId, "CAN ID (hex، مثلاً 1A0):");
    
    _taManualCanId = lv_textarea_create(_manualEntryScreen);
    lv_obj_set_size(_taManualCanId, 220, 35);
    lv_obj_set_pos(_taManualCanId, 10, 116);
    lv_textarea_set_one_line(_taManualCanId, true);
    lv_textarea_set_max_length(_taManualCanId, 8);
    lv_obj_add_event_cb(_taManualCanId, _taFocusEventHandler, LV_EVENT_FOCUSED, NULL);
    
    _manualExtendedCheckbox = lv_checkbox_create(_manualEntryScreen);
    lv_checkbox_set_text(_manualExtendedCheckbox, "شناسه گسترده (29-bit Extended)");
    lv_obj_set_pos(_manualExtendedCheckbox, 10, 156);
    
    lv_obj_t* lblData = lv_label_create(_manualEntryScreen);
    lv_obj_set_pos(lblData, 10, 182);
    lv_label_set_text(lblData, "بایت‌های داده (hex، با فاصله):");
    
    _taManualDataHex = lv_textarea_create(_manualEntryScreen);
    lv_obj_set_size(_taManualDataHex, 220, 35);
    lv_obj_set_pos(_taManualDataHex, 10, 202);
    lv_textarea_set_one_line(_taManualDataHex, true);
    lv_textarea_set_max_length(_taManualDataHex, 30);
    lv_obj_add_event_cb(_taManualDataHex, _taFocusEventHandler, LV_EVENT_FOCUSED, NULL);
    lv_textarea_set_placeholder_text(_taManualDataHex, "01 FF 00 00");
    
    _manualErrorLabel = lv_label_create(_manualEntryScreen);
    lv_obj_set_pos(_manualErrorLabel, 10, 242);
    lv_obj_set_width(_manualErrorLabel, 220);
    lv_label_set_long_mode(_manualErrorLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_manualErrorLabel, "");
    lv_obj_set_style_text_color(_manualErrorLabel, lv_color_hex(0xFF4444), 0);
    
    lv_obj_t* btnSave = lv_btn_create(_manualEntryScreen);
    lv_obj_set_size(btnSave, 105, 40);
    lv_obj_set_pos(btnSave, 10, 275);
    lv_obj_set_style_bg_color(btnSave, lv_color_hex(0x2ECC71), 0);
    lv_obj_add_event_cb(btnSave, _btnManualSubmitEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelSave = lv_label_create(btnSave);
    lv_label_set_text(labelSave, "✅ ذخیره");
    lv_obj_center(labelSave);
    
    lv_obj_t* btnCancel = lv_btn_create(_manualEntryScreen);
    lv_obj_set_size(btnCancel, 105, 40);
    lv_obj_set_pos(btnCancel, 125, 275);
    lv_obj_set_style_bg_color(btnCancel, lv_color_hex(0x555555), 0);
    lv_obj_add_event_cb(btnCancel, _btnManualCancelEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelCancel = lv_label_create(btnCancel);
    lv_label_set_text(labelCancel, "❌ انصراف");
    lv_obj_center(labelCancel);
}

void TFT_UI::_openManualEntryScreen() {
    if (!_manualEntryScreen) return;
    lv_textarea_set_text(_taManualCanId, "");
    lv_textarea_set_text(_taManualDataHex, "");
    lv_label_set_text(_manualErrorLabel, "");
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_manualEntryScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_manualEntryScreen);
}

void TFT_UI::_closeManualEntryScreen() {
    if (!_manualEntryScreen) return;
    lv_obj_add_flag(_manualEntryScreen, LV_OBJ_FLAG_HIDDEN);
}

void TFT_UI::_submitManualEntry() {
    if (!_customStore) return;
    
    uint16_t selectedIdx = lv_dropdown_get_selected(_manualLabelDropdown);
    if (selectedIdx >= LEARN_LABEL_COUNT) return;
    
    const char* canIdStr = lv_textarea_get_text(_taManualCanId);
    const char* dataStr = lv_textarea_get_text(_taManualDataHex);
    bool extended = lv_obj_has_state(_manualExtendedCheckbox, LV_STATE_CHECKED);
    
    if (strlen(canIdStr) == 0) {
        lv_label_set_text(_manualErrorLabel, "CAN ID را وارد کنید");
        return;
    }
    
    uint32_t canId = strtoul(canIdStr, nullptr, 16);
    uint32_t maxId = extended ? 0x1FFFFFFF : 0x7FF;
    if (canId > maxId) {
        lv_label_set_text(_manualErrorLabel, "CAN ID خارج از محدوده مجاز است");
        return;
    }
    
    LearnedCommand cmd;
    strncpy(cmd.label, LEARN_LABEL_OPTIONS[selectedIdx], sizeof(cmd.label) - 1);
    strncpy(cmd.displayName, LEARN_LABEL_DISPLAY_NAMES[selectedIdx], sizeof(cmd.displayName) - 1);
    cmd.canId = canId;
    cmd.isExtended = extended;
    cmd.source = SOURCE_MANUAL;
    cmd.status = CMD_UNVERIFIED;  // طبق بخش ۵ سند - ورودی دستی هم همیشه ابتدا تأییدنشده
    cmd.timesObserved = 0;
    cmd.failCount = 0;
    cmd.createdAt = millis();
    
    // پارس بایت‌های hex جداشده با فاصله
    uint8_t len = 0;
    char buf[64];
    strncpy(buf, dataStr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* token = strtok(buf, " ");
    while (token && len < 8) {
        cmd.data[len++] = (uint8_t)strtoul(token, nullptr, 16);
        token = strtok(nullptr, " ");
    }
    cmd.length = len;
    
    if (len == 0) {
        lv_label_set_text(_manualErrorLabel, "حداقل یک بایت داده وارد کنید");
        return;
    }
    
    bool ok = _customStore->upsertCommand(_selectedProfileForLearning, cmd);
    if (ok) {
        _closeManualEntryScreen();
        showNotification("✅ فرمان دستی ذخیره شد (تأییدنشده)");
        _refreshLearnVehicleList();
    } else {
        lv_label_set_text(_manualErrorLabel, "ذخیره ناموفق بود - آیا پروفایلی انتخاب شده؟");
    }
}

// ======================================================================
// === جدید v2.0: صفحه‌ی تأیید فرمان (بخش ۴.۲ سند) ===
// ⚠️ این تنها صفحه‌ای است که می‌تواند منجر به ارسال واقعی یک فرمان
// UNVERIFIED روی باس شود - و فقط با تأیید دوبار کلیک صریح کاربر.
// ======================================================================

void TFT_UI::_buildVerifyScreen() {
    _verifyScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_verifyScreen, TFT_WIDTH, TFT_HEIGHT);
    lv_obj_set_pos(_verifyScreen, 0, 0);
    lv_obj_set_style_bg_color(_verifyScreen, lv_color_hex(0x2A0F0F), 0);  // پس‌زمینه قرمزتیره - هشداردهنده
    lv_obj_set_style_bg_opa(_verifyScreen, LV_OPA_COVER, 0);
    lv_obj_add_flag(_verifyScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_verifyScreen, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t* title = lv_label_create(_verifyScreen);
    lv_obj_set_pos(title, 10, 8);
    lv_label_set_text(title, "⚠️ تأیید فرمان تست‌نشده");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF4444), 0);
    
    _verifyInfoLabel = lv_label_create(_verifyScreen);
    lv_obj_set_pos(_verifyInfoLabel, 10, 45);
    lv_obj_set_width(_verifyInfoLabel, 220);
    lv_label_set_long_mode(_verifyInfoLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_verifyInfoLabel, "...");
    
    lv_obj_t* btnSend = lv_btn_create(_verifyScreen);
    lv_obj_set_size(btnSend, 220, 45);
    lv_obj_set_pos(btnSend, 10, 160);
    lv_obj_set_style_bg_color(btnSend, lv_color_hex(0xE74C3C), 0);
    lv_obj_add_event_cb(btnSend, _btnVerifySendEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelSend = lv_label_create(btnSend);
    lv_label_set_text(labelSend, "🚨 ارسال آزمایشی یک‌بار");
    lv_obj_center(labelSend);
    
    _verifyResultLabel = lv_label_create(_verifyScreen);
    lv_obj_set_pos(_verifyResultLabel, 10, 215);
    lv_obj_set_width(_verifyResultLabel, 220);
    lv_label_set_long_mode(_verifyResultLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_verifyResultLabel, "");
    
    lv_obj_t* btnSuccess = lv_btn_create(_verifyScreen);
    lv_obj_set_size(btnSuccess, 105, 40);
    lv_obj_set_pos(btnSuccess, 10, 260);
    lv_obj_set_style_bg_color(btnSuccess, lv_color_hex(0x2ECC71), 0);
    lv_obj_add_event_cb(btnSuccess, _btnVerifySuccessEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelSuccess = lv_label_create(btnSuccess);
    lv_label_set_text(labelSuccess, "✅ درست کار کرد");
    lv_obj_center(labelSuccess);
    
    lv_obj_t* btnFail = lv_btn_create(_verifyScreen);
    lv_obj_set_size(btnFail, 105, 40);
    lv_obj_set_pos(btnFail, 125, 260);
    lv_obj_set_style_bg_color(btnFail, lv_color_hex(0x555555), 0);
    lv_obj_add_event_cb(btnFail, _btnVerifyFailEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelFail = lv_label_create(btnFail);
    lv_label_set_text(labelFail, "❌ کار نکرد");
    lv_obj_center(labelFail);
}

void TFT_UI::_openVerifyScreen(uint8_t profileId, const char* label, uint32_t canId,
                               const uint8_t* data, uint8_t length) {
    if (!_verifyScreen) return;
    
    _verifyProfileId = (char)profileId;
    strncpy(_verifyLabel, label, sizeof(_verifyLabel) - 1);
    
    char dataHexStr[32] = {0};
    char* p = dataHexStr;
    for (int i = 0; i < length; i++) {
        p += sprintf(p, "%02X ", data[i]);
    }
    
    char infoBuf[256];
    snprintf(infoBuf, sizeof(infoBuf),
        "با ارسال، پیام زیر مستقیماً روی CAN Bus خودرو فرستاده می‌شود:\n\n"
        "CAN ID: 0x%03X\nData: %s\n\n"
        "این عمل ممکن است اثر فیزیکی فوری روی خودرو داشته باشد. مطمئن "
        "شوید خودرو در وضعیت امنی است.\n\n"
        "⚠️ توجه: بعضی خودروهای جدیدتر (rolling code) ممکن است با این "
        "روش کار نکنند.",
        canId, dataHexStr);
    
    lv_label_set_text(_verifyInfoLabel, infoBuf);
    lv_label_set_text(_verifyResultLabel, "");
    
    lv_obj_clear_flag(_verifyScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_verifyScreen);
}

void TFT_UI::_closeVerifyScreen() {
    if (!_verifyScreen) return;
    lv_obj_add_flag(_verifyScreen, LV_OBJ_FLAG_HIDDEN);
}

void TFT_UI::_onVerifySendPressed() {
    if (!_vehicleControl || !_profileManager) return;
    
    // ⚠️ این خط واقعاً می‌تواند یک فرمان تأییدنشده را روی باس بفرستد -
    // این دقیقاً همان نقطه‌ی طراحی‌شده در بخش ۴.۲ سند است.
    _profileManager->selectCustomVehicle((uint8_t)_verifyProfileId);
    String errReason;
    bool sent = _vehicleControl->executeCommand(_verifyLabel, errReason);
    
    if (sent) {
        lv_label_set_text(_verifyResultLabel, "✅ فرمان ارسال شد. آیا خودرو واکنش درستی نشان داد؟");
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "❌ ارسال ناموفق: %s", errReason.c_str());
        lv_label_set_text(_verifyResultLabel, buf);
    }
}

void TFT_UI::_onVerifyResultPressed(bool success) {
    if (!_customStore) return;
    
    _customStore->setCommandStatus((uint8_t)_verifyProfileId, _verifyLabel,
                                    success ? CMD_VERIFIED : CMD_UNVERIFIED, !success);
    
    showNotification(success ? "✅ فرمان تأیید شد" : "⚠️ فرمان همچنان تأییدنشده باقی ماند");
    _closeVerifyScreen();
    _refreshLearnVehicleList();
}

// ======================== صفحه تغییر رمز (v1.0 - بدون تغییر) ========================

void TFT_UI::_buildPasswordScreen() {
    _passwordScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_passwordScreen, TFT_WIDTH, TFT_HEIGHT);
    lv_obj_set_pos(_passwordScreen, 0, 0);
    lv_obj_set_style_bg_color(_passwordScreen, lv_color_hex(0x0F1A30), 0);
    lv_obj_set_style_bg_opa(_passwordScreen, LV_OPA_COVER, 0);
    lv_obj_add_flag(_passwordScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_passwordScreen, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t* title = lv_label_create(_passwordScreen);
    lv_obj_set_pos(title, 10, 10);
    lv_label_set_text(title, "🔐 تغییر رمز ورود");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    
    lv_obj_t* lblNew = lv_label_create(_passwordScreen);
    lv_obj_set_pos(lblNew, 10, 45);
    lv_label_set_text(lblNew, "رمز جدید (حداقل ۸ کاراکتر):");
    
    _taNewPass = lv_textarea_create(_passwordScreen);
    lv_obj_set_size(_taNewPass, 220, 35);
    lv_obj_set_pos(_taNewPass, 10, 65);
    lv_textarea_set_password_mode(_taNewPass, true);
    lv_textarea_set_one_line(_taNewPass, true);
    lv_textarea_set_max_length(_taNewPass, 15);
    lv_obj_add_event_cb(_taNewPass, _taFocusEventHandler, LV_EVENT_FOCUSED, NULL);
    
    lv_obj_t* lblConfirm = lv_label_create(_passwordScreen);
    lv_obj_set_pos(lblConfirm, 10, 110);
    lv_label_set_text(lblConfirm, "تکرار رمز جدید:");
    
    _taConfirmPass = lv_textarea_create(_passwordScreen);
    lv_obj_set_size(_taConfirmPass, 220, 35);
    lv_obj_set_pos(_taConfirmPass, 10, 130);
    lv_textarea_set_password_mode(_taConfirmPass, true);
    lv_textarea_set_one_line(_taConfirmPass, true);
    lv_textarea_set_max_length(_taConfirmPass, 15);
    lv_obj_add_event_cb(_taConfirmPass, _taFocusEventHandler, LV_EVENT_FOCUSED, NULL);
    
    _passwordErrorLabel = lv_label_create(_passwordScreen);
    lv_obj_set_pos(_passwordErrorLabel, 10, 175);
    lv_label_set_text(_passwordErrorLabel, "");
    lv_obj_set_style_text_color(_passwordErrorLabel, lv_color_hex(0xFF4444), 0);
    lv_label_set_long_mode(_passwordErrorLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_passwordErrorLabel, 220);
    
    lv_obj_t* btnSave = lv_btn_create(_passwordScreen);
    lv_obj_set_size(btnSave, 105, 40);
    lv_obj_set_pos(btnSave, 10, 210);
    lv_obj_set_style_bg_color(btnSave, lv_color_hex(0x2ECC71), 0);
    lv_obj_add_event_cb(btnSave, _btnPasswordSaveEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelSave = lv_label_create(btnSave);
    lv_label_set_text(labelSave, "✅ ذخیره");
    lv_obj_center(labelSave);
    
    lv_obj_t* btnCancel = lv_btn_create(_passwordScreen);
    lv_obj_set_size(btnCancel, 105, 40);
    lv_obj_set_pos(btnCancel, 125, 210);
    lv_obj_set_style_bg_color(btnCancel, lv_color_hex(0x555555), 0);
    lv_obj_add_event_cb(btnCancel, _btnPasswordCancelEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelCancel = lv_label_create(btnCancel);
    lv_label_set_text(labelCancel, "❌ انصراف");
    lv_obj_center(labelCancel);
    
    // کیبورد مجازی مشترک - همچنین توسط صفحات جدید Learn Mode/ورود دستی استفاده می‌شود
    _keyboard = lv_keyboard_create(_passwordScreen == nullptr ? lv_scr_act() : lv_scr_act());
    lv_obj_set_size(_keyboard, TFT_WIDTH, 120);
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
}

void TFT_UI::_openPasswordScreen() {
    if (!_passwordScreen) return;
    lv_textarea_set_text(_taNewPass, "");
    lv_textarea_set_text(_taConfirmPass, "");
    lv_label_set_text(_passwordErrorLabel, "");
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_passwordScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_passwordScreen);
}

void TFT_UI::_closePasswordScreen() {
    if (!_passwordScreen) return;
    lv_obj_add_flag(_passwordScreen, LV_OBJ_FLAG_HIDDEN);
}

void TFT_UI::_refreshPasswordWarning() {
    if (!_passwordWarningLabel) return;
    if (isUsingDefaultPassword()) {
        lv_obj_clear_flag(_passwordWarningLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_passwordWarningLabel, LV_OBJ_FLAG_HIDDEN);
    }
}

void TFT_UI::_submitPasswordChange() {
    if (!_taNewPass || !_taConfirmPass || !_passwordErrorLabel) return;
    
    const char* newPass = lv_textarea_get_text(_taNewPass);
    const char* confirmPass = lv_textarea_get_text(_taConfirmPass);
    
    if (strlen(newPass) < 8) {
        lv_label_set_text(_passwordErrorLabel, "رمز باید حداقل ۸ کاراکتر باشد");
        return;
    }
    if (strcmp(newPass, confirmPass) != 0) {
        lv_label_set_text(_passwordErrorLabel, "تکرار رمز مطابقت ندارد");
        return;
    }
    
    bool ok = setWebPassword(nullptr, newPass);
    if (ok) {
        _closePasswordScreen();
        _refreshPasswordWarning();
        showNotification("✅ رمز با موفقیت تغییر کرد");
    } else {
        lv_label_set_text(_passwordErrorLabel, "رمز باید حداقل ۸ کاراکتر و متفاوت از پیش‌فرض باشد");
    }
}

// ======================== Event Handlers (v1.0 - بدون تغییر) ========================

void TFT_UI::_btnLockEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("lock");
        pThisUI->showNotification("🔒 قفل همه درب‌ها");
    }
}

void TFT_UI::_btnUnlockEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("unlock");
        pThisUI->showNotification("🔓 درب‌ها باز شد");
    }
}

void TFT_UI::_btnWindowUpEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("windows_up");
        pThisUI->showNotification("⬆ شیشه‌ها بالا");
    }
}

void TFT_UI::_btnWindowDownEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("windows_down");
        pThisUI->showNotification("⬇ شیشه‌ها پایین");
    }
}

void TFT_UI::_btnSunroofEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("sunroof");
        pThisUI->showNotification("☀ سانروف تغییر وضعیت");
    }
}

void TFT_UI::_btnTrunkEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("trunk");
        pThisUI->showNotification("🔙 صندوق باز شد");
    }
}

void TFT_UI::_btnMirrorEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("mirror");
        pThisUI->showNotification("🪞 آینه‌ها تا شد");
    }
}

void TFT_UI::_btnAlarmEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("alarm");
        pThisUI->showNotification("🚨 دزدگیر تغییر وضعیت");
    }
}

void TFT_UI::_btnListenOnlyEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("listen_only");
        pThisUI->showNotification("👂 حالت Listen-Only تغییر کرد");
    }
}

void TFT_UI::_btnThemeEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("toggle_theme");
    }
}

void TFT_UI::_btnVehicleSelectEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("vehicle_select");
    }
}

void TFT_UI::_btnChangePasswordEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_openPasswordScreen();
    }
}

void TFT_UI::_btnPasswordSaveEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_submitPasswordChange();
    }
}

void TFT_UI::_btnPasswordCancelEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_closePasswordScreen();
    }
}

// === جدید (چک‌لیست تجاری #9) ===
void TFT_UI::_btnRecalibrateTouchEventHandler(lv_event_t* e) {
    if (pThisUI) {
        // توجه: runTouchCalibration() مسدودکننده است (منتظر لمس کاربر
        // در ۵ نقطه می‌ماند) و مستقیماً روی tft خام کار می‌کند، نه
        // روی LVGL. بعد از پایان، صفحه‌ی LVGL باید دوباره رسم شود
        // (چون runTouchCalibration صفحه را با متن ساده پاک/پر کرده)
        // - lv_obj_invalidate روی صفحه‌ی فعال این کار را انجام می‌دهد.
        pThisUI->runTouchCalibration();
        lv_obj_invalidate(lv_scr_act());
        pThisUI->showNotification("✅ کالیبراسیون لمس به‌روز شد");
    }
}

void TFT_UI::_taFocusEventHandler(lv_event_t* e) {
    if (!pThisUI || !pThisUI->_keyboard) return;
    lv_obj_t* ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(pThisUI->_keyboard, ta);
    lv_obj_clear_flag(pThisUI->_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(pThisUI->_keyboard);
}

// ======================== جدید v2.0: Event Handlers تب یادگیری ========================

void TFT_UI::_btnStartLearnEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_openLearnWizard();
    }
}

void TFT_UI::_btnManualEntryEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_openManualEntryScreen();
    }
}

void TFT_UI::_btnLearnWizardActionEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_onLearnWizardActionPressed();
    }
}

void TFT_UI::_btnLearnWizardCancelEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_closeLearnWizard();
    }
}

void TFT_UI::_btnManualSubmitEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_submitManualEntry();
    }
}

void TFT_UI::_btnManualCancelEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_closeManualEntryScreen();
    }
}

void TFT_UI::_btnVerifySendEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_onVerifySendPressed();
    }
}

void TFT_UI::_btnVerifySuccessEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_onVerifyResultPressed(true);
    }
}

void TFT_UI::_btnVerifyFailEventHandler(lv_event_t* e) {
    if (pThisUI) {
        pThisUI->_onVerifyResultPressed(false);
    }
}

// ======================== به‌روزرسانی اطلاعات Dashboard (v1.0 - بدون تغییر) ========================

void TFT_UI::updateVehicleData(const VehicleData& data) {
    _vehicleData = data;
    
    if (!_initialized) return;
    
    if (_labelSpeed) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d km/h", data.vehicleSpeed);
        lv_label_set_text(_labelSpeed, buf);
    }
    
    if (_labelRPM) {
        char buf[32];
        snprintf(buf, sizeof(buf), "RPM: %d", data.engineRPM);
        lv_label_set_text(_labelRPM, buf);
    }
    
    if (_labelTemp) {
        char buf[32];
        snprintf(buf, sizeof(buf), "🌡 دمای موتور: %d°C", data.coolantTemp);
        lv_label_set_text(_labelTemp, buf);
    }
    
    if (_labelVolt) {
        char buf[32];
        snprintf(buf, sizeof(buf), "🔋 ولتاژ: %.1f V", data.batteryVoltage);
        lv_label_set_text(_labelVolt, buf);
    }
    
    if (_labelFuel) {
        char buf[32];
        snprintf(buf, sizeof(buf), "⛽ سوخت: %d%%", data.fuelLevel);
        lv_label_set_text(_labelFuel, buf);
    }
}

// ======================== تنظیم وضعیت CAN (v1.0 - بدون تغییر) ========================

void TFT_UI::setCANStatus(bool connected) {
    if (_statusCAN) {
        if (connected) {
            lv_label_set_text(_statusCAN, "CAN: ✅ وصل");
            lv_obj_set_style_text_color(_statusCAN, lv_color_hex(0x00FF00), 0);
        } else {
            lv_label_set_text(_statusCAN, "CAN: ❌ قطع");
            lv_obj_set_style_text_color(_statusCAN, lv_color_hex(0xFF0000), 0);
        }
    }
}

// ======================== تنظیم وضعیت WiFi (v1.0 - بدون تغییر) ========================

void TFT_UI::setWiFiStatus(bool connected) {
    if (_statusWiFi) {
        if (connected) {
            lv_label_set_text(_statusWiFi, "WiFi: ✅ وصل");
            lv_obj_set_style_text_color(_statusWiFi, lv_color_hex(0x00FF00), 0);
        } else {
            lv_label_set_text(_statusWiFi, "WiFi: ❌ قطع");
            lv_obj_set_style_text_color(_statusWiFi, lv_color_hex(0xFF0000), 0);
        }
    }
}

// ======================== تنظیم تم (v1.0 - بدون تغییر) ========================

void TFT_UI::setTheme(ThemeMode mode) {
    lv_color_t bgColor;
    lv_color_t fgColor;
    
    switch (mode) {
        case THEME_NIGHT:
            bgColor = lv_color_hex(0x1A1A2E);
            fgColor = lv_color_hex(0xCCCCCC);
            analogWrite(PIN_TFT_BL, TFT_BRIGHTNESS_NIGHT);
            break;
        case THEME_DAY:
        default:
            bgColor = lv_color_hex(0xFFFFFF);
            fgColor = lv_color_hex(0x000000);
            analogWrite(PIN_TFT_BL, TFT_BRIGHTNESS_DAY);
            break;
    }
    
    lv_obj_set_style_bg_color(lv_scr_act(), bgColor, 0);
    Serial.printf("[TFT] تم تغییر کرد: %s\n", 
                  mode == THEME_NIGHT ? "شب" : "روز");
}

// ======================== نمایش نوتیفیکیشن (v1.0 - بدون تغییر) ========================

void TFT_UI::showNotification(const char* message) {
    if (!_initialized) return;
    
    if (_notification) {
        lv_obj_del(_notification);
    }
    
    _notification = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_notification, 220, 35);
    lv_obj_set_pos(_notification, 10, TFT_HEIGHT - 45);
    lv_obj_set_style_bg_color(_notification, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(_notification, 0, 0);
    
    lv_obj_t* label = lv_label_create(_notification);
    lv_label_set_text(label, message);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    
    lv_obj_del_delayed(_notification, 2000);
}

// ======================== تنظیم حالت دستگاه (v1.0 - بدون تغییر) ========================

void TFT_UI::setDeviceMode(DeviceMode mode) {
    _currentMode = mode;
    
    switch (mode) {
        case MODE_SLEEP:
            analogWrite(PIN_TFT_BL, 0);
            break;
        case MODE_ACTIVE:
            analogWrite(PIN_TFT_BL, TFT_BRIGHTNESS_DAY);
            break;
        default:
            break;
    }
}
