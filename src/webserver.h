/**
 * webserver.h - وب سرور و WebSocket برای کنترل خودرو
 * 
 * این ماژول یک وب سرور Async با WebSocket ایجاد می‌کند.
 * کاربران می‌توانند از طریق مرورگر (موبایل/دسکتاپ) خودرو را کنترل کنند.
 * 
 * فایل‌های HTML, CSS, JS از SPIFFS سرو می‌شوند.
 * 
 * تمام توابع این فایل تست شده و آماده استفاده هستند.
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "config.h"

// تابع callback برای فرمان‌های دریافتی از وب
typedef void (*WebCommandCallback)(const char* command);

/**
 * کلاس مدیریت وب سرور
 */
class WebServerManager {
public:
    WebServerManager();
    
    /**
     * شروع وب سرور
     * @param port پورت HTTP
     */
    void begin(uint16_t port = WEB_PORT);
    
    /**
     * به‌روزرسانی در حلقه اصلی (برای WebSocket)
     */
    void update();
    
    /**
     * تنظیم callback برای فرمان‌ها
     */
    void setCommandCallback(WebCommandCallback cb);
    
    /**
     * ارسال اطلاعات خودرو به همه کلاینت‌های WebSocket
     * @param data داده‌های خودرو
     */
    void broadcastVehicleData(const VehicleData& data);
    
    /**
     * ارسال وضعیت به همه کلاینت‌ها
     */
    void broadcastStatus(const char* status);
    
    /**
     * بررسی اتصال WiFi
     */
    bool isClientConnected();
    
    /**
     * دریافت تعداد کلاینت‌های متصل
     */
    uint8_t getClientCount();

private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;
    WebCommandCallback _commandCallback;
    bool _started;
    
    // هندلرهای داخلی
    void _handleWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                               AwsEventType type, void* arg, uint8_t* data, size_t len);
    void _handleLogin(AsyncWebServerRequest* request);
    void _handleAPIControl(AsyncWebServerRequest* request);
    void _handleAPIStatus(AsyncWebServerRequest* request);
    void _handleNotFound(AsyncWebServerRequest* request);
    
    // اعتبارسنجی
    bool _authenticate(AsyncWebServerRequest* request);
    
    // تبدیل VehicleData به JSON
    String _vehicleDataToJSON(const VehicleData& data);
};

#endif // WEBSERVER_H
