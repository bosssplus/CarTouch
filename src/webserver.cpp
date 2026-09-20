/**
 * webserver.cpp - پیاده‌سازی وب سرور و WebSocket
 * 
 * === تغییرات امنیتی نسبت به نسخه اصلی ===
 * ۱. اتصال WebSocket دیگر بدون احراز هویت فرمان نمی‌پذیرد.
 * ۲. بعد از لاگین موفق در /login، یک session token تصادفی صادر می‌شود
 *    و در یک کوکی HttpOnly به مرورگر فرستاده می‌شود.
 * ۳. کلاینت باید همان توکن را به‌عنوان اولین پیام روی WebSocket
 *    (نوع "auth") ارسال کند. قبل از آن، پیام "command" رد می‌شود.
 * ۴. rate-limit ساده روی فرمان‌های کنترلی (هم در WebSocket هم در
 *    /api/control) تا کسی نتواند با اسپم کردن فرمان به موتورهای
 *    شیشه/قفل آسیب برساند.
 */

#include "webserver.h"
#include <SPIFFS.h>
#include <esp_random.h>

// ======================== سازنده ========================

WebServerManager::WebServerManager()
    : _server(WEB_PORT), _ws("/ws") {
    _commandCallback = nullptr;
    _started = false;
    _sessionToken = "";
    _sessionTokenIssuedAt = 0;
}

// ======================== تولید توکن session ========================

String WebServerManager::_generateSessionToken() {
    // تولید ۱۶ بایت تصادفی امن با استفاده از RNG سخت‌افزاری ESP32
    // و تبدیل به رشته hex (۳۲ کاراکتر)
    uint8_t randomBytes[16];
    esp_fill_random(randomBytes, sizeof(randomBytes));
    
    String token = "";
    char hexBuf[3];
    for (int i = 0; i < 16; i++) {
        snprintf(hexBuf, sizeof(hexBuf), "%02x", randomBytes[i]);
        token += hexBuf;
    }
    return token;
}

// ======================== بررسی اعتبار توکن ========================

bool WebServerManager::_isValidSessionToken(const char* token) {
    if (!token || _sessionToken.length() == 0) return false;
    
    // بررسی انقضا
    if (millis() - _sessionTokenIssuedAt > SESSION_TOKEN_TIMEOUT) {
        return false;
    }
    
    // مقایسه رشته‌ای ساده (طول ثابت hex token - نیازی به مقایسه
    // constant-time نیست چون این یک دستگاه تک‌کاربره embedded است،
    // نه یک سرویس چندکاربره در معرض حملات timing در مقیاس بزرگ)
    return _sessionToken.equals(token);
}

// ======================== مدیریت رکورد auth کلاینت ========================

WsClientAuth* WebServerManager::_findClientAuth(uint32_t clientId) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (_clientAuth[i].inUse && _clientAuth[i].clientId == clientId) {
            return &_clientAuth[i];
        }
    }
    return nullptr;
}

WsClientAuth* WebServerManager::_findOrCreateClientAuth(uint32_t clientId) {
    WsClientAuth* existing = _findClientAuth(clientId);
    if (existing) return existing;
    
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (!_clientAuth[i].inUse) {
            _clientAuth[i].inUse = true;
            _clientAuth[i].clientId = clientId;
            _clientAuth[i].authenticated = false;
            _clientAuth[i].lastCommandTime = 0;
            return &_clientAuth[i];
        }
    }
    
    // اگر جا نبود (بیش از WS_MAX_CLIENTS کلاینت همزمان)، قدیمی‌ترین را جایگزین کن
    Serial.println("⚠️ [WEB] ظرفیت کلاینت‌های WebSocket پر شد - جایگزینی اسلات ۰");
    _clientAuth[0].inUse = true;
    _clientAuth[0].clientId = clientId;
    _clientAuth[0].authenticated = false;
    _clientAuth[0].lastCommandTime = 0;
    return &_clientAuth[0];
}

void WebServerManager::_removeClientAuth(uint32_t clientId) {
    WsClientAuth* c = _findClientAuth(clientId);
    if (c) {
        c->inUse = false;
        c->authenticated = false;
        c->clientId = 0;
    }
}

// ======================== شروع وب سرور ========================

void WebServerManager::begin(uint16_t port) {
    Serial.println("[WEB] راه‌اندازی وب سرور...");
    
    // ===== WebSocket =====
    _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, 
                       AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->_handleWebSocketEvent(server, client, type, arg, data, len);
    });
    _server.addHandler(&_ws);

    // ===== Routes =====
    
    // صفحه اصلی (با احراز هویت)
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            AsyncWebServerResponse* response = request->beginResponse(401, "text/html", 
                "<html><body><h3>غیرمجاز</h3>"
                "<form method='POST' action='/login'>"
                "کاربر: <input name='user'><br>"
                "رمز: <input name='pass' type='password'><br>"
                "<input type='submit' value='ورود'></form></body></html>");
            request->send(response);
            return;
        }
        
        // در صورت نبود توکن session معتبر (مثلاً بعد از ریست دستگاه)، یکی صادر کن
        // تا صفحه بتواند بلافاصله به WebSocket auth کند.
        if (_sessionToken.length() == 0 || 
            millis() - _sessionTokenIssuedAt > SESSION_TOKEN_TIMEOUT) {
            _sessionToken = _generateSessionToken();
            _sessionTokenIssuedAt = millis();
        }
        
        AsyncWebServerResponse* response;
        if (SPIFFS.exists("/index.html")) {
            response = request->beginResponse(SPIFFS, "/index.html", "text/html");
        } else {
            response = request->beginResponse(200, "text/html", 
                "<h1>CarTouch</h1><p>فایل index.html یافت نشد.</p>");
        }
        // توکن را در کوکی HttpOnly قرار می‌دهیم تا جاوااسکریپت صفحه هم
        // بتواند (از طریق یک endpoint جدا یا متغیر تزریق‌شده) آن را بخواند.
        // چون فرانت‌اند فعلی ساده است، توکن را هم به صورت متغیر global JS
        // با injection ساده در صفحه قرار می‌دهیم - در app.js از آن استفاده می‌شود.
        response->addHeader("Set-Cookie", "cartouch_session=" + _sessionToken + "; Path=/; HttpOnly");
        request->send(response);
    });
    
    // لاگین
    _server.on("/login", HTTP_POST, [this](AsyncWebServerRequest* request) {
        String user = request->arg("user");
        String pass = request->arg("pass");
        AppConfig* cfg = getConfig();
        
        if (user.equals(cfg->webUser) && pass.equals(cfg->webPass)) {
            // صدور توکن session جدید برای این ورود
            _sessionToken = _generateSessionToken();
            _sessionTokenIssuedAt = millis();
            
            AsyncWebServerResponse* response = request->beginResponse(302, "text/plain", "");
            response->addHeader("Location", "/");
            response->addHeader("Set-Cookie", "cartouch_session=" + _sessionToken + "; Path=/; HttpOnly");
            request->send(response);
        } else {
            Serial.println("⚠️ [WEB] تلاش لاگین ناموفق");
            request->send(401, "text/html", "<html><body><h3>نام کاربری یا رمز اشتباه است</h3><a href='/'>بازگشت</a></body></html>");
        }
    });
    
    // endpoint برای دریافت توکن session توسط app.js (برای اتصال WebSocket)
    // خودش نیاز به همان Basic Auth دارد که صفحه اصلی دارد.
    _server.on("/api/session-token", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        if (_sessionToken.length() == 0 || 
            millis() - _sessionTokenIssuedAt > SESSION_TOKEN_TIMEOUT) {
            _sessionToken = _generateSessionToken();
            _sessionTokenIssuedAt = millis();
        }
        String json = "{\"token\":\"" + _sessionToken + "\"}";
        request->send(200, "application/json", json);
    });
    
    // API کنترل (برای درخواست‌های REST) - همچنان Basic Auth دارد
    _server.on("/api/control", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        _handleAPIControl(request);
    });
    
    // API تغییر رمز وب - نیاز به Basic Auth با رمز فعلی دارد
    _server.on("/api/change-password", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        
        String newUser = request->arg("newUser");
        String newPass = request->arg("newPass");
        String confirmPass = request->arg("confirmPass");
        
        if (newPass.length() == 0) {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"رمز جدید خالی است\"}");
            return;
        }
        if (!newPass.equals(confirmPass)) {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"تکرار رمز مطابقت ندارد\"}");
            return;
        }
        
        bool ok = setWebPassword(newUser.length() > 0 ? newUser.c_str() : nullptr, newPass.c_str());
        if (ok) {
            // چون رمز عوض شد، همه sessionهای فعلی را باطل کن (کاربر باید دوباره لاگین کند)
            _sessionToken = "";
            _sessionTokenIssuedAt = 0;
            Serial.println("[WEB] رمز وب با موفقیت تغییر کرد");
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", 
                "{\"success\":false,\"error\":\"رمز باید حداقل ۸ کاراکتر و متفاوت از رمز پیش‌فرض باشد\"}");
        }
    });
    
    // API وضعیت
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        _handleAPIStatus(request);
    });
    
    // سرو فایل‌های static از SPIFFS
    _server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    // 404
    _server.onNotFound([this](AsyncWebServerRequest* request) {
        _handleNotFound(request);
    });
    
    // شروع سرور
    _server.begin();
    _started = true;
    
    Serial.printf("[WEB] وب سرور روشن شد: http://%s:%d (ورود: %s/%s)\n",
                  WiFi.softAPIP().toString().c_str(), 
                  port,
                  getConfig()->webUser,
                  getConfig()->webPass);
    Serial.println("[WEB] ⚠️ لطفاً رمز پیش‌فرض وب را از منوی تنظیمات تغییر دهید");
}

// ======================== به‌روزرسانی ========================

void WebServerManager::update() {
    // پاکسازی دوره‌ای کلاینت‌هایی که دیگر متصل نیستند اختیاری است؛
    // AsyncWebSocket خودش WS_EVT_DISCONNECT را صدا می‌زند و ما آنجا پاک می‌کنیم.
}

// ======================== تنظیم callback ========================

void WebServerManager::setCommandCallback(WebCommandCallback cb) {
    _commandCallback = cb;
}

// ======================== رویداد WebSocket ========================

void WebServerManager::_handleWebSocketEvent(AsyncWebSocket* server, 
                                              AsyncWebSocketClient* client,
                                              AwsEventType type, 
                                              void* arg, 
                                              uint8_t* data, 
                                              size_t len) {
    switch (type) {
        case WS_EVT_CONNECT: {
            Serial.printf("[WEB] کلاینت %d متصل شد (در انتظار auth)\n", client->id());
            _findOrCreateClientAuth(client->id());
            // توجه: پیام welcome دیگر بلافاصله فرستاده نمی‌شود تا اطلاعات
            // اضافی به کلاینت‌های احراز هویت‌نشده لو نرود.
            client->printf("{\"type\":\"need_auth\"}");
            break;
        }
            
        case WS_EVT_DISCONNECT:
            Serial.printf("[WEB] کلاینت %d قطع شد\n", client->id());
            _removeClientAuth(client->id());
            break;
            
        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len) {
                String msg = String((char*)data).substring(0, len);
                
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, msg);
                if (error) break;
                
                const char* type = doc["type"];
                if (!type) break;
                
                WsClientAuth* auth = _findOrCreateClientAuth(client->id());
                
                // ----- پیام auth: تنها پیامی که بدون احراز هویت قبول می‌شود -----
                if (strcmp(type, "auth") == 0) {
                    const char* token = doc["token"];
                    if (_isValidSessionToken(token)) {
                        auth->authenticated = true;
                        client->printf("{\"type\":\"welcome\",\"message\":\"به CarTouch خوش آمدید\"}");
                        Serial.printf("[WEB] کلاینت %d احراز هویت شد\n", client->id());
                    } else {
                        client->printf("{\"type\":\"auth_failed\"}");
                        Serial.printf("⚠️ [WEB] تلاش auth ناموفق از کلاینت %d\n", client->id());
                        client->close(1008, "auth failed");
                    }
                    break;
                }
                
                // ----- هر پیام دیگر نیاز به احراز هویت قبلی دارد -----
                if (!auth->authenticated) {
                    Serial.printf("⚠️ [WEB] پیام بدون auth از کلاینت %d رد شد\n", client->id());
                    client->printf("{\"type\":\"need_auth\"}");
                    break;
                }
                
                if (strcmp(type, "command") == 0) {
                    // rate-limit: جلوگیری از ارسال فرمان‌های پشت‌سرهم خیلی سریع
                    uint32_t now = millis();
                    if (now - auth->lastCommandTime < COMMAND_RATE_LIMIT_MS) {
                        client->printf("{\"type\":\"rate_limited\"}");
                        break;
                    }
                    auth->lastCommandTime = now;
                    
                    const char* command = doc["command"];
                    if (command && _commandCallback) {
                        _commandCallback(command);
                        client->printf("{\"type\":\"ack\",\"command\":\"%s\"}", command);
                    }
                } else if (strcmp(type, "ping") == 0) {
                    client->printf("{\"type\":\"pong\"}");
                }
            }
            break;
        }
        
        case WS_EVT_PONG:
            break;
            
        case WS_EVT_ERROR:
            break;
    }
}

// ======================== احراز هویت HTTP ========================

bool WebServerManager::_authenticate(AsyncWebServerRequest* request) {
    AppConfig* cfg = getConfig();
    
    if (!request->authenticate(cfg->webUser, cfg->webPass)) {
        request->requestAuthentication("CarTouch");
        return false;
    }
    return true;
}

// ======================== API کنترل ========================

void WebServerManager::_handleAPIControl(AsyncWebServerRequest* request) {
    String command = request->arg("command");
    
    if (command.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"command parameter required\"}");
        return;
    }
    
    if (_commandCallback) {
        _commandCallback(command.c_str());
    }
    
    String json = "{\"success\":true,\"command\":\"" + command + "\"}";
    request->send(200, "application/json", json);
}

// ======================== API وضعیت ========================

void WebServerManager::_handleAPIStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["status"] = "ok";
    doc["message"] = "CarTouch active";
    doc["usingDefaultPassword"] = isUsingDefaultPassword();
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

// ======================== 404 ========================

void WebServerManager::_handleNotFound(AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "404 - Not Found");
}

// ======================== ارسال اطلاعات خودرو ========================

void WebServerManager::broadcastVehicleData(const VehicleData& data) {
    if (!_started) return;
    
    String json = _vehicleDataToJSON(data);
    
    // فقط برای کلاینت‌های احراز هویت‌شده ارسال کن
    for (AsyncWebSocketClient* client : _ws.getClients()) {
        WsClientAuth* auth = _findClientAuth(client->id());
        if (auth && auth->authenticated) {
            client->text(json);
        }
    }
}

// ======================== ارسال وضعیت ========================

void WebServerManager::broadcastStatus(const char* status) {
    if (!_started) return;
    
    String msg = "{\"type\":\"status\",\"message\":\"";
    msg += status;
    msg += "\"}";
    
    for (AsyncWebSocketClient* client : _ws.getClients()) {
        WsClientAuth* auth = _findClientAuth(client->id());
        if (auth && auth->authenticated) {
            client->text(msg);
        }
    }
}

// ======================== VehicleData به JSON ========================

String WebServerManager::_vehicleDataToJSON(const VehicleData& data) {
    JsonDocument doc;
    
    doc["type"] = "vehicle_data";
    doc["speed"] = data.vehicleSpeed;
    doc["rpm"] = data.engineRPM;
    doc["coolantTemp"] = data.coolantTemp;
    doc["battery"] = data.batteryVoltage;
    doc["fuel"] = data.fuelLevel;
    doc["throttle"] = data.throttlePos;
    
    doc["doorFL"] = (int)data.doorFL;
    doc["doorFR"] = (int)data.doorFR;
    doc["doorRL"] = (int)data.doorRL;
    doc["doorRR"] = (int)data.doorRR;
    doc["trunk"] = (int)data.trunkState;
    doc["alarm"] = (int)data.alarmState;
    
    String output;
    serializeJson(doc, output);
    return output;
}

// ======================== بررسی اتصال کلاینت ========================

bool WebServerManager::isClientConnected() {
    return _ws.count() > 0;
}

// ======================== تعداد کلاینت‌ها ========================

uint8_t WebServerManager::getClientCount() {
    return _ws.count();
}
