/**
 * webserver.h - وب سرور و WebSocket برای کنترل خودرو
 * 
 * === اصلاحیه امنیتی مهم ===
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
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "config.h"

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
};

#endif // WEBSERVER_H
