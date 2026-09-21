/**
 * webserver.h - وب سرور و WebSocket برای کنترل خودرو
 * 
 * === اصلاحیه امنیتی مهم (حفظ‌شده از v1.0) ===
 * قبلاً اتصال WebSocket هیچ احراز هویتی نداشت: هرکسی که IP دستگاه را
 * می‌دانست می‌توانست مستقیماً به /ws وصل شود و فرمان قفل/بازکردن درب،
 * صندوق و غیره بفرستد، بدون رد شدن از صفحه لاگین.
 *
 * روش رفع: بعد از لاگین موفق در HTTP (/login)، یک توکن تصادفی یک‌بارمصرف
 * (session token) به مرورگر داده می‌شود. کلاینت باید همین توکن را در
 * اولین پیام WebSocket (نوع "auth") بفرستد. تا وقتی کلاینت auth نشده،
 * هیچ فرمان "command"ای از او پذیرفته نمی‌شود.
 *
 * این یک لایه دفاعی سبک مناسب دستگاه embedded است، نه JWT/OAuth کامل؛
 * اما به‌مراتب بهتر از نبود کامل احراز هویت در نسخه قبلی است.
 * 
 * === CarTouch v2.0 - افزوده‌های جدید ===
 * به CarTouch_V2_SPEC.md بخش ۷.۵ مراجعه کنید. پیام‌های WebSocket جدید
 * برای Learn Mode (learn_start, learn_capture_baseline, ...) و
 * endpoint های REST جدید برای مدیریت پروفایل‌های سفارشی اضافه شده‌اند.
 * همه‌ی این‌ها از همان زیرساخت auth/rate-limit موجود عبور می‌کنند -
 * هیچ مسیر موازی بدون احراز هویت اضافه نشده است.
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "config.h"
#include "learn_engine.h"
#include "custom_vehicle_store.h"
#include "active_profile_manager.h"
#include "vehicle_control.h"

// حداکثر تعداد کلاینت‌های WebSocket که وضعیت auth آن‌ها را همزمان ردیابی می‌کنیم
#define WS_MAX_CLIENTS 8

// مهلت اعتبار توکن session (میلی‌ثانیه) - ۱۵ دقیقه
#define SESSION_TOKEN_TIMEOUT 900000

// حداقل فاصله زمانی مجاز بین دو فرمان کنترلی از یک کلاینت (میلی‌ثانیه)
#define COMMAND_RATE_LIMIT_MS 300

// تابع callback برای فرمان‌های دریافتی از وب
typedef void (*WebCommandCallback)(const char* command);

// وضعیت احراز هویت هر کلاینت WebSocket
struct WsClientAuth {
    uint32_t clientId = 0;
    bool authenticated = false;
    uint32_t lastCommandTime = 0;
    bool inUse = false;
};

/**
 * کلاس مدیریت وب سرور
 */
class WebServerManager {
public:
    WebServerManager();
    
    void begin(uint16_t port = WEB_PORT);
    void update();
    void setCommandCallback(WebCommandCallback cb);
    void broadcastVehicleData(const VehicleData& data);
    void broadcastStatus(const char* status);
    bool isClientConnected();
    uint8_t getClientCount();
    
    /**
     * === جدید در v2.0 ===
     * اتصال ماژول‌های Learn Mode / پروفایل سفارشی. باید قبل از
     * begin() یک‌بار در setup() صدا زده شود (مشابه setCommandCallback).
     * این متد جداست تا سازنده‌ی WebServerManager دست‌نخورده بماند و
     * ترتیب مقداردهی اولیه در main.cpp انعطاف‌پذیر بماند.
     */
    void attachLearnModules(LearnEngine* learnEngine, 
                            CustomVehicleStore* customStore,
                            ActiveProfileManager* profileManager,
                            VehicleControl* vehicleControl);

private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;
    WebCommandCallback _commandCallback;
    bool _started;
    
    // توکن session فعلی (تولید شده بعد از لاگین موفق) و زمان صدور آن
    String _sessionToken;
    uint32_t _sessionTokenIssuedAt;
    
    // وضعیت auth هر کلاینت WebSocket متصل
    WsClientAuth _clientAuth[WS_MAX_CLIENTS];
    
    // === جدید در v2.0: پوینترهای ماژول‌های Learn Mode ===
    // پوینتر (نه reference) چون ممکن است در ابتدا (قبل از
    // attachLearnModules) هنوز مقداردهی نشده باشند؛ همه‌ی متدهایی که
    // از این‌ها استفاده می‌کنند باید null بودن را چک کنند.
    LearnEngine* _learnEngine;
    CustomVehicleStore* _customStore;
    ActiveProfileManager* _profileManager;
    VehicleControl* _vehicleControl;
    
    void _handleWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                               AwsEventType type, void* arg, uint8_t* data, size_t len);
    void _handleAPIControl(AsyncWebServerRequest* request);
    void _handleAPIStatus(AsyncWebServerRequest* request);
    void _handleNotFound(AsyncWebServerRequest* request);
    
    bool _authenticate(AsyncWebServerRequest* request);
    String _generateSessionToken();
    bool _isValidSessionToken(const char* token);
    
    WsClientAuth* _findOrCreateClientAuth(uint32_t clientId);
    WsClientAuth* _findClientAuth(uint32_t clientId);
    void _removeClientAuth(uint32_t clientId);
    
    String _vehicleDataToJSON(const VehicleData& data);
    
    // === جدید در v2.0: پردازش پیام‌های WebSocket مربوط به Learn Mode ===
    // این تابع فقط بعد از احراز هویت کامل کلاینت (auth->authenticated==true)
    // صدا زده می‌شود - دقیقاً مثل پیام‌های "command" موجود.
    void _handleLearnModeMessage(AsyncWebSocketClient* client, JsonDocument& doc, const char* type);
    
    // ساخت JSON برای وضعیت فعلی learn engine (برای ارسال به کلاینت)
    String _learnStateToJSON();
    
    // === جدید در v2.0: REST endpoint های مدیریت پروفایل سفارشی ===
    void _registerCustomVehicleRoutes();
};

#endif // WEBSERVER_H
