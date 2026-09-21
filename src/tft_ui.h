/**
 * tft_ui.h - رابط کاربری صفحه لمسی با LVGL (CarTouch v2.0)
 * 
 * این ماژول رابط کاربری اصلی پروژه است.
 * از کتابخانه LVGL نسخه 8 برای طراحی رابط گرافیکی استفاده می‌کند.
 * 
 * صفحه دارای چهار تب اصلی است (تب چهارم جدید در v2.0):
 *   1. کنترل (Control) - دکمه‌های کنترلی بزرگ
 *   2. داشبورد (Dashboard) - نمایش اطلاعات خودرو
 *   3. تنظیمات (Settings) - تنظیمات دستگاه
 *   4. یادگیری (Learn) - یادگیری فرمان از CAN Bus واقعی + ورود دستی +
 *      لیست پروفایل‌های سفارشی با وضعیت تأیید (جدید - به
 *      CarTouch_V2_SPEC.md بخش ۷.۴ مراجعه کنید)
 * 
 * === معماری اتصال به Learn Mode ===
 * برای حفظ جدایی لایه‌ها (مثل الگوی UIControlCallback موجود)، این
 * کلاس مستقیماً وابسته به LearnEngine/CustomVehicleStore نیست، بلکه
 * از طریق پوینتر (که main.cpp تزریق می‌کند) با آن‌ها کار می‌کند - این
 * تصمیم طراحی باعث می‌شود tft_ui.h نیازی به include کردن تمام
 * هدرهای Learn Mode نداشته باشد و کدی که از قبل کامپایل می‌شد کمتر
 * دستخوش تغییر بی‌جهت شود.
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

// پیش‌اعلان (forward declaration) کلاس‌های Learn Mode - جزئیات در
// main.cpp/learn_engine.h/custom_vehicle_store.h
class LearnEngine;
class CustomVehicleStore;
class ActiveProfileManager;
class VehicleControl;

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
     * === جدید v2.0 ===
     * اتصال ماژول‌های Learn Mode. باید قبل از begin() صدا زده شود
     * (مشابه الگوی attachLearnModules در WebServerManager).
     */
    void attachLearnModules(LearnEngine* learnEngine,
                            CustomVehicleStore* customStore,
                            ActiveProfileManager* profileManager,
                            VehicleControl* vehicleControl);
    
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
    
    // === جدید v2.0: پوینترهای ماژول‌های Learn Mode ===
    LearnEngine* _learnEngine;
    CustomVehicleStore* _customStore;
    ActiveProfileManager* _profileManager;
    VehicleControl* _vehicleControl;
    
    // LVGL objects
    lv_obj_t* _tabView;
    lv_obj_t* _tabControl;
    lv_obj_t* _tabDashboard;
    lv_obj_t* _tabSettings;
    lv_obj_t* _tabLearn;           // === جدید v2.0 ===
    
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
    
    // ==================== صفحه تغییر رمز (v1.0 - بدون تغییر) ====================
    lv_obj_t* _passwordScreen;
    lv_obj_t* _passwordWarningLabel;
    lv_obj_t* _taNewPass;
    lv_obj_t* _taConfirmPass;
    lv_obj_t* _passwordErrorLabel;
    lv_obj_t* _keyboard;             // کیبورد مجازی مشترک (هم برای رمز، هم برای Learn Mode استفاده می‌شود)
    
    // ==================== جدید v2.0: تب یادگیری ====================
    // زیرصفحه‌ی اصلی تب: لیست وسایل نقلیه سفارشی + دکمه "یادگیری جدید" + "ورود دستی"
    lv_obj_t* _learnMainContainer;
    lv_obj_t* _learnVehicleList;      // لیست پروفایل‌های سفارشی با وضعیت
    lv_obj_t* _learnActiveVehicleLabel;
    
    // مودال Wizard یادگیری (state machine بخش ۴.۱ سند)
    lv_obj_t* _learnWizardScreen;
    lv_obj_t* _learnWizardTitle;
    lv_obj_t* _learnWizardStatusLabel;
    lv_obj_t* _learnWizardProgressBar;
    lv_obj_t* _learnWizardCandidateList; // لیست کاندیدها بعد از capture
    lv_obj_t* _learnWizardActionBtn;     // دکمه‌ی متن‌متغیر (شروع/ادامه/تأیید)
    lv_obj_t* _learnWizardCancelBtn;
    lv_obj_t* _learnLabelDropdown;       // انتخاب برچسب فرمان از پیش‌تعریف‌شده
    int _selectedCandidateIndex;         // ایندکس کاندید انتخاب‌شده توسط کاربر (-1 = هیچ)
    
    // مودال ورود دستی (بخش ۵ سند)
    lv_obj_t* _manualEntryScreen;
    lv_obj_t* _taManualCanId;
    lv_obj_t* _taManualDataHex;
    lv_obj_t* _manualLabelDropdown;
    lv_obj_t* _manualExtendedCheckbox;
    lv_obj_t* _manualErrorLabel;
    
    // مودال تأیید فرمان (بخش ۴.۲ سند) - نمایش صریح CAN ID/بایت قبل از ارسال آزمایشی
    lv_obj_t* _verifyScreen;
    lv_obj_t* _verifyInfoLabel;
    lv_obj_t* _verifyResultLabel;
    char _verifyProfileId;
    char _verifyLabel[32];
    
    uint8_t _selectedProfileForLearning;  // پروفایلی که در حال یادگیری/ورود دستی برایش هستیم
    
    // تابع‌های داخلی برای ساختن صفحات (v1.0)
    void _buildTabControl();
    void _buildTabDashboard();
    void _buildTabSettings();
    void _buildPasswordScreen();
    void _openPasswordScreen();
    void _closePasswordScreen();
    void _submitPasswordChange();
    void _refreshPasswordWarning();
    
    // === جدید v2.0: تابع‌های داخلی تب یادگیری ===
    void _buildTabLearn();
    void _refreshLearnVehicleList();
    void _buildLearnWizardScreen();
    void _openLearnWizard();
    void _closeLearnWizard();
    void _refreshLearnWizardUI();   // بر اساس learnEngine->getState() نمایش را به‌روز می‌کند
    void _onLearnWizardActionPressed();
    void _onLearnCandidateSelected(int index);
    void _saveLearnedCommand();
    
    void _buildManualEntryScreen();
    void _openManualEntryScreen();
    void _closeManualEntryScreen();
    void _submitManualEntry();
    
    void _buildVerifyScreen();
    void _openVerifyScreen(uint8_t profileId, const char* label, uint32_t canId, 
                           const uint8_t* data, uint8_t length);
    void _closeVerifyScreen();
    void _onVerifySendPressed();
    void _onVerifyResultPressed(bool success);
    
    // Event handlers (v1.0 - بدون تغییر)
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
    static void _btnChangePasswordEventHandler(lv_event_t* e);
    static void _btnPasswordSaveEventHandler(lv_event_t* e);
    static void _btnPasswordCancelEventHandler(lv_event_t* e);
    static void _taFocusEventHandler(lv_event_t* e);
    
    // === جدید v2.0: Event handlers تب یادگیری ===
    static void _btnStartLearnEventHandler(lv_event_t* e);
    static void _btnManualEntryEventHandler(lv_event_t* e);
    static void _btnLearnWizardActionEventHandler(lv_event_t* e);
    static void _btnLearnWizardCancelEventHandler(lv_event_t* e);
    static void _learnCandidateSelectEventHandler(lv_event_t* e);
    static void _btnManualSubmitEventHandler(lv_event_t* e);
    static void _btnManualCancelEventHandler(lv_event_t* e);
    static void _btnVerifySendEventHandler(lv_event_t* e);
    static void _btnVerifySuccessEventHandler(lv_event_t* e);
    static void _btnVerifyFailEventHandler(lv_event_t* e);
    static void _vehicleListItemEventHandler(lv_event_t* e);
    
    // LVGL display buffer
    static lv_disp_draw_buf_t _dispBuf;
    static lv_color_t _buf1[LVGL_BUF_SIZE];
    static lv_color_t _buf2[LVGL_BUF_SIZE];
    
    // توابع display driver
    static void _lvglDisplayFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* colorMap);
    static void _lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data);
};

#endif // TFT_UI_H
