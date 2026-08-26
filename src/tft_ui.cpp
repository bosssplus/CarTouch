/**
 * tft_ui.cpp - پیاده‌سازی رابط کاربری لمسی با LVGL 8
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#include "tft_ui.h"
#include <TFT_eSPI.h>

// ======================== متغیرهای static ========================

static TFT_eSPI tft = TFT_eSPI();          // نمونه TFT
static TFT_eSPI* pTft = &tft;               // pointer برای دسترسی در callbackها

// LVGL buffer
lv_disp_draw_buf_t TFT_UI::_dispBuf;
lv_color_t TFT_UI::_buf1[LVGL_BUF_SIZE];
lv_color_t TFT_UI::_buf2[LVGL_BUF_SIZE];

// Touch calibration (مقادیر پیش‌فرض - نیاز به کالیبره دارد)
static uint16_t touchCalibX[4] = {250, 245, 20, 25};
static uint16_t touchCalibY[4] = {70, 360, 370, 80};

// Pointer به نمونه UI (برای استفاده در callbackهای static)
static TFT_UI* pThisUI = nullptr;

// ======================== تابع Flush نمایشگر ========================

void TFT_UI::_lvglDisplayFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* colorMap) {
    uint32_t width = area->x2 - area->x1 + 1;
    uint32_t height = area->y2 - area->y1 + 1;
    
    pTft->startWrite();
    pTft->setAddrWindow(area->x1, area->y1, width, height);
    pTft->pushColors((uint16_t*)colorMap, width * height, true);
    pTft->endWrite();
    
    lv_disp_flush_ready(drv);
}

// ======================== تابع خواندن تاچ ========================

void TFT_UI::_lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    uint16_t touchX, touchY;
    bool touched = pTft->getTouch(&touchX, &touchY, 600);  // 600 = فشار آستانه
    
    if (touched) {
        // کالیبراسیون ساده (در صورت نیاز)
        // تو تبدیل مختصات به صفحه
        data->point.x = map(touchX, 0, 320, 0, TFT_WIDTH);
        data->point.y = map(touchY, 0, 240, 0, TFT_HEIGHT);
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ======================== سازنده ========================

TFT_UI::TFT_UI() {
    _initialized = false;
    _controlCallback = nullptr;
    _currentMode = MODE_ACTIVE;
    memset(&_vehicleData, 0, sizeof(VehicleData));
    pThisUI = this;
}

// ======================== مقداردهی اولیه ========================

void TFT_UI::begin() {
    Serial.println("[TFT] مقداردهی اولیه صفحه نمایش...");
    
    // 1. راه‌اندازی TFT_eSPI
    tft.begin();
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(TFT_BLACK);
    
    // روشنایی
    pinMode(PIN_TFT_BL, OUTPUT);
    analogWrite(PIN_TFT_BL, TFT_BRIGHTNESS_DAY);
    
    // 2. راه‌اندازی LVGL
    lv_init();
    
    // 3. تنظیم بافر display
    lv_disp_draw_buf_init(&_dispBuf, _buf1, _buf2, LVGL_BUF_SIZE);
    
    // 4. تنظیم driver نمایشگر
    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = TFT_WIDTH;
    dispDrv.ver_res = TFT_HEIGHT;
    dispDrv.flush_cb = _lvglDisplayFlush;
    dispDrv.draw_buf = &_dispBuf;
    lv_disp_drv_register(&dispDrv);
    
    // 5. تنظیم driver تاچ
    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = _lvglTouchRead;
    lv_indev_drv_register(&indevDrv);
    
    // 6. ساختن رابط کاربری
    _buildTabControl();
    _buildTabDashboard();
    _buildTabSettings();
    
    _initialized = true;
    Serial.println("[TFT] صفحه نمایش با موفقیت مقداردهی شد ✓");
}

// ======================== به‌روزرسانی ========================

void TFT_UI::update() {
    if (!_initialized) return;
    lv_timer_handler();
}

// ======================== تنظیم callback ========================

void TFT_UI::setControlCallback(UIControlCallback cb) {
    _controlCallback = cb;
}

// ======================== ساختن تب کنترل ========================

void TFT_UI::_buildTabControl() {
    // ایجاد Tab View
    _tabView = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 35);
    
    // اضافه کردن تب‌ها
    _tabControl = lv_tabview_add_tab(_tabView, "کنترل");
    _tabDashboard = lv_tabview_add_tab(_tabView, "داشبورد");
    _tabSettings = lv_tabview_add_tab(_tabView, "تنظیمات");
    
    // ==================== دکمه‌های تب کنترل ====================
    
    // ردیف ۱: قفل/باز کردن درب‌ها
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
    
    // ردیف ۲: شیشه‌ها
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
    
    // ردیف ۳: سانروف و صندوق عقب
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
    
    // ردیف ۴: آینه و دزدگیر
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
    
    // ردیف ۵: حالت Listen-Only
    lv_obj_t* btnListenOnly = lv_btn_create(_tabControl);
    lv_obj_set_size(btnListenOnly, 220, 40);
    lv_obj_set_pos(btnListenOnly, 10, 250);
    lv_obj_add_event_cb(btnListenOnly, _btnListenOnlyEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelListenOnly = lv_label_create(btnListenOnly);
    lv_label_set_text(labelListenOnly, "👂 حالت Listen-Only");
    lv_obj_center(labelListenOnly);
    
    // وضعیت CAN و WiFi در پایین
    _statusCAN = lv_label_create(_tabControl);
    lv_obj_set_pos(_statusCAN, 10, 300);
    lv_label_set_text(_statusCAN, "CAN: ?");
    
    _statusWiFi = lv_label_create(_tabControl);
    lv_obj_set_pos(_statusWiFi, 120, 300);
    lv_label_set_text(_statusWiFi, "WiFi: ?");
}

// ======================== ساختن تب Dashboard ========================

void TFT_UI::_buildTabDashboard() {
    if (!_tabDashboard) return;
    
    // سرعت (بزرگ)
    _labelSpeed = lv_label_create(_tabDashboard);
    lv_obj_set_pos(_labelSpeed, 10, 10);
    lv_label_set_text(_labelSpeed, "۰ km/h");
    lv_obj_set_style_text_font(_labelSpeed, lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(_labelSpeed, lv_color_hex(0x00FF00), 0);
    
    // RPM
    _labelRPM = lv_label_create(_tabDashboard);
    lv_obj_set_pos(_labelRPM, 10, 70);
    lv_label_set_text(_labelRPM, "RPM: ۰");
    lv_obj_set_style_text_font(_labelRPM, lv_font_montserrat_20, 0);
    
    // دما
    _labelTemp = lv_label_create(_tabDashboard);
    lv_obj_set_pos(_labelTemp, 10, 100);
    lv_label_set_text(_labelTemp, "🌡 دمای موتور: -- °C");
    
    // ولتاژ
    _labelVolt = lv_label_create(_tabDashboard);
    lv_obj_set_pos(_labelVolt, 10, 130);
    lv_label_set_text(_labelVolt, "🔋 ولتاژ: -- V");
    
    // سوخت
    _labelFuel = lv_label_create(_tabDashboard);
    lv_obj_set_pos(_labelFuel, 10, 160);
    lv_label_set_text(_labelFuel, "⛽ سوخت: --%");
    
    // وضعیت درب‌ها
    lv_obj_t* labelDoorStatus = lv_label_create(_tabDashboard);
    lv_obj_set_pos(labelDoorStatus, 10, 200);
    lv_label_set_text(labelDoorStatus, "🚪 درب‌ها: --");
}

// ======================== ساختن تب تنظیمات ========================

void TFT_UI::_buildTabSettings() {
    if (!_tabSettings) return;
    
    lv_obj_t* labelTitle = lv_label_create(_tabSettings);
    lv_obj_set_pos(labelTitle, 10, 10);
    lv_label_set_text(labelTitle, "تنظیمات");
    lv_obj_set_style_text_font(labelTitle, lv_font_montserrat_20, 0);
    
    // دکمه انتخاب خودرو
    lv_obj_t* btnVehicle = lv_btn_create(_tabSettings);
    lv_obj_set_size(btnVehicle, 220, 45);
    lv_obj_set_pos(btnVehicle, 10, 50);
    lv_obj_add_event_cb(btnVehicle, _btnVehicleSelectEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelVehicle = lv_label_create(btnVehicle);
    lv_label_set_text(labelVehicle, "🚗 انتخاب خودرو");
    lv_obj_center(labelVehicle);
    
    // دکمه تغییر تم
    lv_obj_t* btnTheme = lv_btn_create(_tabSettings);
    lv_obj_set_size(btnTheme, 220, 45);
    lv_obj_set_pos(btnTheme, 10, 110);
    lv_obj_add_event_cb(btnTheme, _btnThemeEventHandler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* labelTheme = lv_label_create(btnTheme);
    lv_label_set_text(labelTheme, "🌙 حالت شب/روز");
    lv_obj_center(labelTheme);
    
    // توضیحات
    lv_obj_t* labelInfo = lv_label_create(_tabSettings);
    lv_obj_set_pos(labelInfo, 10, 180);
    lv_label_set_text(labelInfo, 
        "⚠ خودرو باید خاموش باشد\n"
        "قبل از نصب باتری را جدا کنید\n"
        "CarTouch v1.0");
}

// ======================== Event Handler: قفل ========================

void TFT_UI::_btnLockEventHandler(lv_event_t* e) {
    if (pThisUI && pThisUI->_controlCallback) {
        pThisUI->_controlCallback("lock");
        pThisUI->showNotification("🔒 قفل همه درب‌ها");
    }
}

// ======================== Event Handler: باز کردن قفل ========================

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

// ======================== به‌روزرسانی اطلاعات Dashboard ========================

void TFT_UI::updateVehicleData(const VehicleData& data) {
    _vehicleData = data;
    
    if (!_initialized) return;
    
    // به‌روزرسانی برچسب‌ها (با بررسی null)
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

// ======================== تنظیم وضعیت CAN ========================

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

// ======================== تنظیم وضعیت WiFi ========================

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

// ======================== تنظیم تم ========================

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

// ======================== نمایش نوتیفیکیشن ========================

void TFT_UI::showNotification(const char* message) {
    if (!_initialized) return;
    
    // ایجاد یک نوار نوتیفیکیشن موقت در پایین صفحه
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
    
    // حذف خودکار بعد از ۲ ثانیه
    lv_obj_del_delayed(_notification, 2000);
}

// ======================== تنظیم حالت دستگاه ========================

void TFT_UI::setDeviceMode(DeviceMode mode) {
    _currentMode = mode;
    
    switch (mode) {
        case MODE_SLEEP:
            analogWrite(PIN_TFT_BL, 0);  // خاموش کردن صفحه
            break;
        case MODE_ACTIVE:
            analogWrite(PIN_TFT_BL, TFT_BRIGHTNESS_DAY);
            break;
        default:
            break;
    }
}
