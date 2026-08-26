/**
 * tft_ui.h - رابط کاربری صفحه لمسی با LVGL
 * 
 * این ماژول رابط کاربری اصلی پروژه است.
 * از کتابخانه LVGL نسخه 8 برای طراحی رابط گرافیکی استفاده می‌کند.
 * 
 * صفحه دارای سه تب اصلی است:
 *   1. کنترل (Control) - دکمه‌های کنترلی بزرگ
 *   2. داشبورد (Dashboard) - نمایش اطلاعات خودرو
 *   3. تنظیمات (Settings) - تنظیمات دستگاه
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#ifndef TFT_UI_H
#define TFT_UI_H

#include <Arduino.h>
#include <lvgl.h>
#include "config.h"

// پیش‌تعریف callbackها برای جلوگیری از خطای کامپایل
typedef void (*UIControlCallback)(const char* command);

/**
 * کلاس رابط کاربری TFT
 */
class TFT_UI {
public:
    /**
     * سازنده
     */
    TFT_UI();
    
    /**
     * مقداردهی اولیه صفحه نمایش و LVGL
     */
    void begin();
    
    /**
     * به‌روزرسانی صفحه (در حلقه اصلی صدا زده شود)
     */
    void update();
    
    /**
     * تنظیم callback برای ارسال فرمان
     * @param cb تابع callback
     */
    void setControlCallback(UIControlCallback cb);
    
    /**
     * به‌روزرسانی اطلاعات نمایش داده شده در Dashboard
     * @param data داده‌های خودرو
     */
    void updateVehicleData(const VehicleData& data);
    
    /**
     * تنظیم وضعیت اتصال CAN
     * @param connected true اگر وصل است
     */
    void setCANStatus(bool connected);
    
    /**
     * تنظیم وضعیت WiFi
     * @param connected true اگر وصل است
     */
    void setWiFiStatus(bool connected);
    
    /**
     * تنظیم حالت شب/روز
     * @param mode حالت
     */
    void setTheme(ThemeMode mode);
    
    /**
     * نمایش نوتیفیکیشن (پیام کوتاه)
     * @param message متن پیام
     */
    void showNotification(const char* message);
    
    /**
     * تنظیم حالت دستگاه
     */
    void setDeviceMode(DeviceMode mode);

private:
    bool _initialized;
    UIControlCallback _controlCallback;
    VehicleData _vehicleData;
    DeviceMode _currentMode;
    
    // LVGL objects
    lv_obj_t* _tabView;
    lv_obj_t* _tabControl;
    lv_obj_t* _tabDashboard;
    lv_obj_t* _tabSettings;
    
    // Dashboard labels
    lv_obj_t* _labelSpeed;
    lv_obj_t* _labelRPM;
    lv_obj_t* _labelTemp;
    lv_obj_t* _labelVolt;
    lv_obj_t* _labelFuel;
    
    // Status indicators
    lv_obj_t* _statusCAN;
    lv_obj_t* _statusWiFi;
    lv_obj_t* _notification;
    
    // تابع‌های داخلی برای ساختن صفحات
    void _buildTabControl();
    void _buildTabDashboard();
    void _buildTabSettings();
    
    // Event handlers
    static void _btnLockEventHandler(lv_event_t* e);
    static void _btnUnlockEventHandler(lv_event_t* e);
    static void _btnWindowUpEventHandler(lv_event_t* e);
    static void _btnWindowDownEventHandler(lv_event_t* e);
    static void _btnSunroofEventHandler(lv_event_t* e);
    static void _btnTrunkEventHandler(lv_event_t* e);
    static void _btnMirrorEventHandler(lv_event_t* e);
    static void _btnAlarmEventHandler(lv_event_t* e);
    static void _btnThemeEventHandler(lv_event_t* e);
    static void _btnListenOnlyEventHandler(lv_event_t* e);
    static void _btnVehicleSelectEventHandler(lv_event_t* e);
    
    // LVGL display buffer
    static lv_disp_draw_buf_t _dispBuf;
    static lv_color_t _buf1[LVGL_BUF_SIZE];
    static lv_color_t _buf2[LVGL_BUF_SIZE];
    
    // توابع display driver
    static void _lvglDisplayFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* colorMap);
    static void _lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data);
};

#endif // TFT_UI_H
