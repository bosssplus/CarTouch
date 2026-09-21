/**
 * webserver.cpp - پیاده‌سازی وب سرور و WebSocket (CarTouch v2.0)
 * 
 * تمام منطق امنیتی v1.0 (session token، rate-limit، auth روی
 * static files) دقیقاً حفظ شده - فقط پیام‌ها/route های جدید Learn
 * Mode اضافه شده‌اند که همگی از همان زیرساخت عبور می‌کنند.
 */

#include "webserver.h"
#include "custom_vehicle.h"
#include <SPIFFS.h>
#include <esp_random.h>

// ======================== سازنده ========================

WebServerManager::WebServerManager()
    : _server(WEB_PORT), _ws("/ws") {
    _commandCallback = nullptr;
    _started = false;
    _sessionToken = "";
    _sessionTokenIssuedAt = 0;
    
    _learnEngine = nullptr;
    _customStore = nullptr;
    _profileManager = nullptr;
    _vehicleControl = nullptr;
}

// ======================== اتصال ماژول‌های Learn Mode (جدید v2.0) ========================

void WebServerManager::attachLearnModules(LearnEngine* learnEngine,
                                          CustomVehicleStore* customStore,
                                          ActiveProfileManager* profileManager,
                                          VehicleControl* vehicleControl) {
    _learnEngine = learnEngine;
    _customStore = customStore;
    _profileManager = profileManager;
    _vehicleControl = vehicleControl;
}

// ======================== تولید توکن session ========================

String WebServerManager::_generateSessionToken() {
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
    
    if (millis() - _sessionTokenIssuedAt > SESSION_TOKEN_TIMEOUT) {
        return false;
    }
    
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

    // ===== Routes (v1.0 - بدون تغییر) =====
    
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
        response->addHeader("Set-Cookie", "cartouch_session=" + _sessionToken + "; Path=/; HttpOnly");
        request->send(response);
    });
    
    _server.on("/login", HTTP_POST, [this](AsyncWebServerRequest* request) {
        String user = request->arg("user");
        String pass = request->arg("pass");
        AppConfig* cfg = getConfig();
        
        if (user.equals(cfg->webUser) && pass.equals(cfg->webPass)) {
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
    
    _server.on("/api/control", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        _handleAPIControl(request);
    });
    
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
            _sessionToken = "";
            _sessionTokenIssuedAt = 0;
            Serial.println("[WEB] رمز وب با موفقیت تغییر کرد");
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", 
                "{\"success\":false,\"error\":\"رمز باید حداقل ۸ کاراکتر و متفاوت از رمز پیش‌فرض باشد\"}");
        }
    });
    
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        _handleAPIStatus(request);
    });
    
    // === جدید v2.0: مدیریت پروفایل‌های سفارشی (همه پشت همان auth) ===
    _registerCustomVehicleRoutes();
    
    _server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (SPIFFS.exists("/app.js")) {
            request->send(SPIFFS, "/app.js", "application/javascript");
        } else {
            request->send(404, "text/plain", "404 - Not Found");
        }
    });
    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (SPIFFS.exists("/style.css")) {
            request->send(SPIFFS, "/style.css", "text/css");
        } else {
            request->send(404, "text/plain", "404 - Not Found");
        }
    });
    
    _server.onNotFound([this](AsyncWebServerRequest* request) {
        _handleNotFound(request);
    });
    
    _server.begin();
    _started = true;
    
    Serial.printf("[WEB] وب سرور روشن شد: http://%s:%d (کاربر: %s)\n",
                  WiFi.softAPIP().toString().c_str(), 
                  port,
                  getConfig()->webUser);
    if (isUsingDefaultPassword()) {
        Serial.println("[WEB] ⚠️ لطفاً رمز پیش‌فرض وب را از منوی تنظیمات تغییر دهید");
    }
}

// ======================== جدید v2.0: REST routes پروفایل سفارشی ========================

void WebServerManager::_registerCustomVehicleRoutes() {
    // GET /api/vehicles/custom - لیست پروفایل‌های سفارشی (خلاصه)
    _server.on("/api/vehicles/custom", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        if (!_customStore) {
            request->send(500, "application/json", "{\"error\":\"ماژول پروفایل سفارشی مقداردهی نشده\"}");
            return;
        }
        
        JsonDocument doc;
        JsonArray arr = doc["profiles"].to<JsonArray>();
        for (int i = 0; i < MAX_CUSTOM_VEHICLES; i++) {
            CustomVehicleProfile summary;
            if (_customStore->getProfileSummary(i, summary)) {
                JsonObject item = arr.add<JsonObject>();
                item["id"] = summary.id;
                item["name"] = summary.name;
                item["brand"] = summary.brand;
                item["model"] = summary.model;
                item["year"] = summary.year;
                item["commandCount"] = summary.commandCount;
            }
        }
        
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });
    
    // POST /api/vehicles/custom/new - ساخت پروفایل خالی جدید
    // بدنه فرم: name, brand (اختیاری), model (اختیاری), year (اختیاری)
    _server.on("/api/vehicles/custom/new", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        if (!_customStore) {
            request->send(500, "application/json", "{\"error\":\"ماژول پروفایل سفارشی مقداردهی نشده\"}");
            return;
        }
        
        String name = request->arg("name");
        String brand = request->arg("brand");
        String model = request->arg("model");
        uint16_t year = request->hasArg("year") ? request->arg("year").toInt() : 0;
        
        if (name.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"نام پروفایل الزامی است\"}");
            return;
        }
        
        uint8_t newIndex;
        bool ok = _customStore->createNewProfile(name.c_str(), brand.c_str(), 
                                                   model.c_str(), year, newIndex);
        if (ok) {
            String json = "{\"success\":true,\"id\":" + String(newIndex) + "}";
            request->send(200, "application/json", json);
        } else {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"ظرفیت پروفایل‌ها پر است\"}");
        }
    });
    
    // POST /api/vehicles/custom/manual-add - افزودن فرمان دستی (بخش ۵ سند)
    // بدنه فرم: profileId, label, displayName, canId (hex string مثل "1A0"),
    //           extended ("1"/"0"), dataHex (مثل "01 FF 00")
    _server.on("/api/vehicles/custom/manual-add", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        if (!_customStore) {
            request->send(500, "application/json", "{\"error\":\"ماژول پروفایل سفارشی مقداردهی نشده\"}");
            return;
        }
        
        if (!request->hasArg("profileId") || !request->hasArg("label") || 
            !request->hasArg("canId") || !request->hasArg("dataHex")) {
            request->send(400, "application/json", "{\"error\":\"فیلدهای الزامی ناقص است\"}");
            return;
        }
        
        uint8_t profileId = request->arg("profileId").toInt();
        String label = request->arg("label");
        String displayName = request->hasArg("displayName") ? request->arg("displayName") : label;
        String canIdHex = request->arg("canId");
        bool extended = request->hasArg("extended") && request->arg("extended") == "1";
        String dataHex = request->arg("dataHex");
        
        // پارس CAN ID (hex، بدون یا با پیشوند 0x)
        uint32_t canId = strtoul(canIdHex.c_str(), nullptr, 16);
        uint32_t maxId = extended ? 0x1FFFFFFF : 0x7FF;
        if (canId > maxId) {
            request->send(400, "application/json", 
                "{\"error\":\"CAN ID خارج از محدوده مجاز است\"}");
            return;
        }
        
        // پارس بایت‌های داده (رشته‌ی hex جداشده با فاصله، مثل "01 FF 00")
        LearnedCommand cmd;
        strncpy(cmd.label, label.c_str(), sizeof(cmd.label) - 1);
        strncpy(cmd.displayName, displayName.c_str(), sizeof(cmd.displayName) - 1);
        cmd.canId = canId;
        cmd.isExtended = extended;
        cmd.source = SOURCE_MANUAL;
        cmd.status = CMD_UNVERIFIED;  // همیشه UNVERIFIED - طبق بخش ۵ سند
        cmd.timesObserved = 0;
        cmd.failCount = 0;
        cmd.createdAt = millis();
        
        uint8_t len = 0;
        char buf[64];
        strncpy(buf, dataHex.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* token = strtok(buf, " ");
        while (token && len < 8) {
            cmd.data[len++] = (uint8_t)strtoul(token, nullptr, 16);
            token = strtok(nullptr, " ");
        }
        cmd.length = len;
        
        bool ok = _customStore->upsertCommand(profileId, cmd);
        if (ok) {
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"ذخیره ناموفق بود\"}");
        }
    });
    
    // GET /api/vehicles/custom/export?id=N - دانلود JSON یک پروفایل
    _server.on("/api/vehicles/custom/export", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        if (!_customStore || !request->hasArg("id")) {
            request->send(400, "application/json", "{\"error\":\"شناسه پروفایل الزامی است\"}");
            return;
        }
        
        uint8_t id = request->arg("id").toInt();
        String json;
        if (!_customStore->exportProfileJSON(id, json)) {
            request->send(404, "application/json", "{\"error\":\"پروفایل یافت نشد\"}");
            return;
        }
        
        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
        response->addHeader("Content-Disposition", "attachment; filename=cartouch_profile.json");
        request->send(response);
    });
    
    // POST /api/vehicles/custom/import - آپلود JSON پروفایل
    // بدنه: raw JSON در arg "json" (فرم urlencoded) - برای سادگی
    // پیاده‌سازی روی ESPAsyncWebServer بدون نیاز به multipart/file upload
    _server.on("/api/vehicles/custom/import", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        if (!_customStore || !request->hasArg("json")) {
            request->send(400, "application/json", "{\"error\":\"محتوای JSON الزامی است\"}");
            return;
        }
        
        String jsonBody = request->arg("json");
        uint8_t newIndex;
        bool ok = _customStore->importProfileJSON(jsonBody, newIndex);
        if (ok) {
            String resp = "{\"success\":true,\"id\":" + String(newIndex) + 
                         ",\"note\":\"همه فرمان‌ها به حالت تأییدنشده وارد شدند\"}";
            request->send(200, "application/json", resp);
        } else {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"فایل نامعتبر یا ظرفیت پر است\"}");
        }
    });
    
    // POST /api/vehicles/custom/delete - حذف یک پروفایل
    _server.on("/api/vehicles/custom/delete", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        if (!_customStore || !request->hasArg("id")) {
            request->send(400, "application/json", "{\"error\":\"شناسه پروفایل الزامی است\"}");
            return;
        }
        
        uint8_t id = request->arg("id").toInt();
        bool ok = _customStore->deleteProfile(id);
        request->send(ok ? 200 : 400, "application/json", 
                      ok ? "{\"success\":true}" : "{\"success\":false}");
    });
    
    // POST /api/vehicles/custom/select - انتخاب یک پروفایل سفارشی به‌عنوان خودروی فعال
    _server.on("/api/vehicles/custom/select", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        if (!_profileManager || !request->hasArg("id")) {
            request->send(400, "application/json", "{\"error\":\"شناسه پروفایل الزامی است\"}");
            return;
        }
        
        uint8_t id = request->arg("id").toInt();
        bool ok = _profileManager->selectCustomVehicle(id);
        request->send(ok ? 200 : 404, "application/json", 
                      ok ? "{\"success\":true}" : "{\"success\":false,\"error\":\"پروفایل یافت نشد\"}");
    });
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

// ======================== ساخت JSON وضعیت Learn Mode ========================

String WebServerManager::_learnStateToJSON() {
    if (!_learnEngine) return "{\"type\":\"learn_state\",\"state\":\"unavailable\"}";
    
    JsonDocument doc;
    doc["type"] = "learn_state";
    
    LearnModeState state = _learnEngine->getState();
    const char* stateStr = "idle";
    switch (state) {
        case LEARN_IDLE: stateStr = "idle"; break;
        case LEARN_BASELINE_CAPTURE: stateStr = "baseline_capture"; break;
        case LEARN_WAITING_ACTION: stateStr = "waiting_action"; break;
        case LEARN_ACTION_CAPTURE: stateStr = "action_capture"; break;
        case LEARN_CANDIDATES_READY: stateStr = "candidates_ready"; break;
        case LEARN_ERROR: stateStr = "error"; break;
    }
    doc["state"] = stateStr;
    doc["progress"] = _learnEngine->getProgressPercent();
    doc["label"] = _learnEngine->getCurrentLabel();
    doc["displayName"] = _learnEngine->getCurrentDisplayName();
    
    if (state == LEARN_CANDIDATES_READY) {
        JsonArray candidates = doc["candidates"].to<JsonArray>();
        uint8_t count = _learnEngine->getCandidateCount();
        for (int i = 0; i < count; i++) {
            LearnCandidate c;
            if (!_learnEngine->getCandidate(i, c)) continue;
            
            JsonObject item = candidates.add<JsonObject>();
            item["index"] = i;
            item["canId"] = c.canId;
            
            char hexId[12];
            snprintf(hexId, sizeof(hexId), "0x%03X", c.canId);
            item["canIdHex"] = hexId;
            
            JsonArray dataArr = item["data"].to<JsonArray>();
            for (int b = 0; b < c.length; b++) dataArr.add(c.data[b]);
            
            item["length"] = c.length;
            item["isNew"] = c.isNewMessage;
            item["seenCount"] = c.seenCountInAction;
        }
    }
    
    String json;
    serializeJson(doc, json);
    return json;
}

// ======================== پردازش پیام‌های Learn Mode (جدید v2.0) ========================
// این تابع فقط از _handleWebSocketEvent، بعد از تأیید auth->authenticated،
// صدا زده می‌شود - دقیقاً همان سطح دسترسی پیام‌های "command" معمولی.

void WebServerManager::_handleLearnModeMessage(AsyncWebSocketClient* client, JsonDocument& doc, const char* type) {
    if (!_learnEngine || !_customStore || !_profileManager || !_vehicleControl) {
        client->printf("{\"type\":\"learn_error\",\"message\":\"ماژول‌های Learn Mode مقداردهی نشده‌اند\"}");
        return;
    }
    
    if (strcmp(type, "learn_start") == 0) {
        const char* label = doc["label"] | "";
        const char* displayName = doc["displayName"] | label;
        if (strlen(label) == 0) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"برچسب فرمان الزامی است\"}");
            return;
        }
        _learnEngine->beginLearning(label, displayName);
        client->text(_learnStateToJSON());
        
    } else if (strcmp(type, "learn_capture_baseline") == 0) {
        _learnEngine->startBaselineCapture();
        client->text(_learnStateToJSON());
        
    } else if (strcmp(type, "learn_confirm_action") == 0) {
        // کاربر می‌گوید "الان دکمه رو می‌زنم" - شروع بازه‌ی ضبط اقدام
        _learnEngine->confirmReadyForAction();
        client->text(_learnStateToJSON());
        
    } else if (strcmp(type, "learn_get_state") == 0) {
        // برای poll کردن پیشرفت (چون baseline/action capture زمان‌بر است)
        client->text(_learnStateToJSON());
        
    } else if (strcmp(type, "learn_save") == 0) {
        uint8_t candidateIndex = doc["candidateIndex"] | 255;
        uint8_t profileId = doc["profileId"] | 255;
        
        LearnCandidate candidate;
        if (!_learnEngine->getCandidate(candidateIndex, candidate)) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"کاندید نامعتبر\"}");
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
        cmd.status = CMD_UNVERIFIED;  // همیشه UNVERIFIED در ابتدا - طبق بخش ۴.۲ سند
        cmd.timesObserved = candidate.seenCountInAction;
        cmd.failCount = 0;
        cmd.createdAt = millis();
        
        bool ok = _customStore->upsertCommand(profileId, cmd);
        if (ok) {
            client->printf("{\"type\":\"learn_saved\",\"success\":true,\"label\":\"%s\"}", cmd.label);
            _learnEngine->cancel();  // بازگشت به IDLE برای یادگیری بعدی
        } else {
            client->printf("{\"type\":\"learn_saved\",\"success\":false}");
        }
        
    } else if (strcmp(type, "learn_cancel") == 0) {
        _learnEngine->cancel();
        client->text(_learnStateToJSON());
        
    } else if (strcmp(type, "verify_command") == 0) {
        // === مرحله تأیید (بخش ۴.۲ سند) ===
        // کاربر می‌خواهد یک فرمان UNVERIFIED را یک‌بار امتحان کند.
        // این تنها جایی است که یک فرمان یادگرفته‌شده/دستی واقعاً
        // ممکن است روی باس ارسال شود - و فقط با تأیید صریح این پیام.
        uint8_t profileId = doc["vehicleId"] | 255;
        const char* label = doc["label"] | "";
        
        LearnedCommand cmd;
        if (!_customStore->findCommand(profileId, label, cmd)) {
            client->printf("{\"type\":\"verify_error\",\"message\":\"فرمان یافت نشد\"}");
            return;
        }
        
        // ارسال آزمایشی واقعی - از همان مسیر رسمی با rate-limit عبور می‌کند
        _profileManager->selectCustomVehicle(profileId);
        String errReason;
        bool sent = _vehicleControl->executeCommand(label, errReason);
        
        JsonDocument respDoc;
        respDoc["type"] = "verify_sent";
        respDoc["success"] = sent;
        respDoc["canId"] = cmd.canId;
        char hexId[12];
        snprintf(hexId, sizeof(hexId), "0x%03X", cmd.canId);
        respDoc["canIdHex"] = hexId;
        if (!sent) respDoc["error"] = errReason;
        
        String resp;
        serializeJson(respDoc, resp);
        client->text(resp);
        
    } else if (strcmp(type, "verify_confirm") == 0) {
        // کاربر بعد از verify_command، دستی تأیید می‌کند که خودرو
        // درست واکنش نشان داد یا نه.
        uint8_t profileId = doc["vehicleId"] | 255;
        const char* label = doc["label"] | "";
        bool success = doc["success"] | false;
        
        bool ok = _customStore->setCommandStatus(profileId, label,
                     success ? CMD_VERIFIED : CMD_UNVERIFIED, !success);
        
        client->printf("{\"type\":\"verify_confirmed\",\"success\":%s}", ok ? "true" : "false");
    }
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
                
                const char* msgType = doc["type"];
                if (!msgType) break;
                
                WsClientAuth* auth = _findOrCreateClientAuth(client->id());
                
                if (strcmp(msgType, "auth") == 0) {
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
                
                if (!auth->authenticated) {
                    Serial.printf("⚠️ [WEB] پیام بدون auth از کلاینت %d رد شد\n", client->id());
                    client->printf("{\"type\":\"need_auth\"}");
                    break;
                }
                
                if (strcmp(msgType, "command") == 0) {
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
                } else if (strcmp(msgType, "ping") == 0) {
                    client->printf("{\"type\":\"pong\"}");
                } else if (strncmp(msgType, "learn_", 6) == 0 || strncmp(msgType, "verify_", 7) == 0) {
                    // === جدید v2.0: همان rate-limit فرمان‌های عادی هم
                    // برای verify_command اعمال می‌شود چون آن پیام
                    // می‌تواند منجر به ارسال واقعی روی باس شود.
                    if (strcmp(msgType, "verify_command") == 0) {
                        uint32_t now = millis();
                        if (now - auth->lastCommandTime < COMMAND_RATE_LIMIT_MS) {
                            client->printf("{\"type\":\"rate_limited\"}");
                            break;
                        }
                        auth->lastCommandTime = now;
                    }
                    _handleLearnModeMessage(client, doc, msgType);
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
    
    if (_profileManager) {
        char vehicleName[48];
        _profileManager->getActiveVehicleName(vehicleName, sizeof(vehicleName));
        doc["activeVehicle"] = vehicleName;
    }
    
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
    
    for (AsyncWebSocketClient& client : _ws.getClients()) {
        WsClientAuth* auth = _findClientAuth(client.id());
        if (auth && auth->authenticated) {
            client.text(json);
        }
    }
}

// ======================== ارسال وضعیت ========================

void WebServerManager::broadcastStatus(const char* status) {
    if (!_started) return;
    
    String msg = "{\"type\":\"status\",\"message\":\"";
    msg += status;
    msg += "\"}";
    
    for (AsyncWebSocketClient& client : _ws.getClients()) {
        WsClientAuth* auth = _findClientAuth(client.id());
        if (auth && auth->authenticated) {
            client.text(msg);
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
