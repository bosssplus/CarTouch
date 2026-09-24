/**
 * webserver.cpp - Web server and WebSocket implementation (CarTouch v2.0)
 *
 * All v1.0 security logic (session token, rate limiting, auth on static
 * files) is preserved unchanged - only the new Learn Mode messages/
 * routes were added, all going through the same infrastructure.
 *
 * User-facing strings (HTML page text, JSON error messages shown in the
 * UI) are in Persian throughout, since that is the product's actual
 * language - only code comments below are in English.
 */

#include "webserver.h"
#include "custom_vehicle.h"
#include <SPIFFS.h>
#include <esp_random.h>
#include <Update.h>

// ============================================================================
// OTA page (embedded in firmware, independent of SPIFFS)
// ============================================================================
// Deliberately kept in firmware rather than SPIFFS: this page still
// works even if the web asset files are corrupted.
static const char OTA_PAGE_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="fa" dir="rtl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CarTouch - به‌روزرسانی</title>
<style>
body{font-family:sans-serif;background:#0f1a30;color:#eee;margin:0;padding:16px}
.card{background:#16213e;border-radius:10px;padding:14px;margin-bottom:14px}
h3{margin:0 0 8px}
input[type=file]{width:100%;margin:8px 0}
button{width:100%;padding:12px;border:0;border-radius:8px;background:#e94560;color:#fff;font-size:1rem}
button:disabled{opacity:.5}
progress{width:100%;height:14px;margin-top:8px}
.msg{margin-top:8px;font-size:.9rem}
.warn{color:#f5a623;font-size:.85rem}
a{color:#7fb3ff}
</style></head><body>
<div class="card"><h3>🔄 فریمویر (firmware.bin)</h3>
<input type="file" id="f-fw" accept=".bin">
<button id="b-fw">آپلود و نصب</button>
<progress id="p-fw" value="0" max="100" hidden></progress>
<div class="msg" id="m-fw"></div></div>
<div class="card"><h3>🗂 فایل‌های وب (spiffs.bin)</h3>
<p class="warn">⚠️ با نصب این فایل، همه‌ی داده‌های روی فایل‌سیستم (از جمله پروفایل‌های سفارشی حالت یادگیری) پاک می‌شود.</p>
<input type="file" id="f-fs" accept=".bin">
<button id="b-fs">آپلود و نصب</button>
<progress id="p-fs" value="0" max="100" hidden></progress>
<div class="msg" id="m-fs"></div></div>
<p><a href="/">← بازگشت</a></p>
<script>
function up(t){
  var f=document.getElementById('f-'+t).files[0],
      m=document.getElementById('m-'+t),
      p=document.getElementById('p-'+t),
      b=document.getElementById('b-'+t);
  if(!f){m.textContent='اول یک فایل .bin انتخاب کنید';return;}
  var x=new XMLHttpRequest(),fd=new FormData();
  fd.append('file',f,f.name);
  b.disabled=true;p.hidden=false;p.value=0;
  m.textContent='در حال ارسال... برد را خاموش نکنید و صفحه را نبندید';
  x.upload.onprogress=function(e){if(e.lengthComputable)p.value=e.loaded*100/e.total;};
  x.onload=function(){
    b.disabled=false;var r;
    try{r=JSON.parse(x.responseText);}catch(e){r={ok:false,msg:'پاسخ نامعتبر ('+x.status+')'};}
    m.textContent=(r.ok?'✅ ':'❌ ')+r.msg;
  };
  x.onerror=function(){b.disabled=false;m.textContent='❌ ارتباط قطع شد';};
  x.open('POST','/update?type='+t);
  x.send(fd);
}
document.getElementById('b-fw').onclick=function(){up('fw');};
document.getElementById('b-fs').onclick=function(){up('fs');};
</script></body></html>)rawliteral";

// ============================================================================
// Constructor
// ============================================================================

WebServerManager* WebServerManager::_instance = nullptr;

WebServerManager::WebServerManager()
    : _server(WEB_PORT), _ws("/ws") {
    _commandCallback      = nullptr;
    _started                = false;
    _sessionToken             = "";
    _sessionTokenIssuedAt        = 0;

    _otaError          = "";
    _otaBytes            = 0;
    _otaIsFs               = false;
    _rebootPending            = false;
    _rebootAt                   = 0;

    _learnEngine        = nullptr;
    _customStore           = nullptr;
    _profileManager           = nullptr;
    _vehicleControl              = nullptr;

    // Current architecture assumes a single WebServerManager instance
    // (matching webServer in main.cpp). If multiple instances are ever
    // created, this pattern would need to become a list/vector.
    _instance = this;
    registerPasswordChangeCallback(&WebServerManager::_staticInvalidateSessions);
}

void WebServerManager::_staticInvalidateSessions() {
    if (_instance) {
        _instance->invalidateAllSessions();
    }
}

void WebServerManager::invalidateAllSessions() {
    // 1. Invalidate the HTTP session token - previously only done inside
    // the web's own password-change route.
    _sessionToken = "";
    _sessionTokenIssuedAt = 0;

    // 2. Invalidate every connected WebSocket client's auth state - this
    // didn't exist before at all. Without it, even after the session
    // token was cleared, a WebSocket client already marked
    // authenticated=true could still send control commands, since
    // _isValidSessionToken is only checked on the initial "auth"
    // message, not on every subsequent command.
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        _clientAuth[i].authenticated = false;
    }

    Serial.println("[WEB] All web sessions invalidated (password changed)");
}

// ============================================================================
// Learn Mode module wiring
// ============================================================================

void WebServerManager::attachLearnModules(LearnEngine* learnEngine,
                                          CustomVehicleStore* customStore,
                                          ActiveProfileManager* profileManager,
                                          VehicleControl* vehicleControl) {
    _learnEngine     = learnEngine;
    _customStore      = customStore;
    _profileManager     = profileManager;
    _vehicleControl       = vehicleControl;
}

// ============================================================================
// Session token
// ============================================================================

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

bool WebServerManager::_isValidSessionToken(const char* token) {
    if (!token || _sessionToken.length() == 0) return false;

    if (millis() - _sessionTokenIssuedAt > SESSION_TOKEN_TIMEOUT) {
        return false;
    }

    return _sessionToken.equals(token);
}

// ============================================================================
// Client auth record management
// ============================================================================

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
            _clientAuth[i].inUse             = true;
            _clientAuth[i].clientId            = clientId;
            _clientAuth[i].authenticated         = false;
            _clientAuth[i].lastCommandTime          = 0;
            return &_clientAuth[i];
        }
    }

    Serial.println("[WEB] WebSocket client capacity full - reusing slot 0");
    _clientAuth[0].inUse             = true;
    _clientAuth[0].clientId            = clientId;
    _clientAuth[0].authenticated         = false;
    _clientAuth[0].lastCommandTime          = 0;
    return &_clientAuth[0];
}

void WebServerManager::_removeClientAuth(uint32_t clientId) {
    WsClientAuth* c = _findClientAuth(clientId);
    if (c) {
        c->inUse           = false;
        c->authenticated      = false;
        c->clientId              = 0;
    }
}

// ============================================================================
// begin()
// ============================================================================

void WebServerManager::begin(uint16_t port) {
    Serial.println("[WEB] Starting web server...");

    // -- WebSocket ----------------------------------------------------------
    _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                       AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->_handleWebSocketEvent(server, client, type, arg, data, len);
    });
    _server.addHandler(&_ws);

    // -- Routes ---------------------------------------------------------------

    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        // Previously, _authenticate() would send its own 401 response
        // and then this handler would also send a second response; the
        // second response had no WWW-Authenticate header, so the
        // browser never showed the credentials prompt and the user got
        // stuck on a dead-end page. Now exactly one response is sent,
        // carrying both a readable body and the browser's credentials
        // prompt.
        AppConfig* authCfg = getConfig();
        if (!request->authenticate(authCfg->webUser, authCfg->webPass)) {
            AsyncWebServerResponse* response = request->beginResponse(401, "text/html; charset=utf-8",
                "<html><head><meta charset='utf-8'></head><body dir='rtl'><h3>غیرمجاز</h3>"
                "<p>نام کاربری و رمز را در پنجره‌ی مرورگر وارد کنید. اگر پنجره‌ای نیامد، صفحه را دوباره باز کنید.</p>"
                "</body></html>");
            response->addHeader("WWW-Authenticate", "Basic realm=\"CarTouch\"");
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
            response = request->beginResponse(SPIFFS, "/index.html", "text/html; charset=utf-8");
        } else {
            response = request->beginResponse(200, "text/html; charset=utf-8",
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
            Serial.println("[WEB] Failed login attempt");
            request->send(401, "text/html; charset=utf-8", "<html><head><meta charset='utf-8'></head><body dir='rtl'><h3>نام کاربری یا رمز اشتباه است</h3><a href='/'>بازگشت</a></body></html>");
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

        String newUser      = request->arg("newUser");
        String newPass         = request->arg("newPass");
        String confirmPass        = request->arg("confirmPass");

        if (newPass.length() == 0) {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"رمز جدید خالی است\"}");
            return;
        }
        if (!newPass.equals(confirmPass)) {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"تکرار رمز مطابقت ندارد\"}");
            return;
        }

        // Manually clearing _sessionToken here used to be duplicated;
        // this is now handled automatically and identically (both for
        // this route and for the TFT) by the callback registered in
        // this class's constructor - see invalidateAllSessions() and
        // registerPasswordChangeCallback.
        bool ok = setWebPassword(newUser.length() > 0 ? newUser.c_str() : nullptr, newPass.c_str());
        if (ok) {
            Serial.println("[WEB] Web password changed successfully");
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

    // -- OTA: firmware/filesystem update via browser (behind the same auth) --------
    _server.on("/update", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) return;
        request->send(200, "text/html; charset=utf-8", OTA_PAGE_HTML);
    });
    _server.on("/update", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            _handleOtaFinished(request);
        },
        [this](AsyncWebServerRequest* request, const String& filename,
               size_t index, uint8_t* data, size_t len, bool final) {
            _handleOtaUpload(request, filename, index, data, len, final);
        });

    // -- Custom profile management routes (all behind the same auth) -----------------
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

    Serial.printf("[WEB] Web server started: http://%s:%d (user: %s)\n",
                  WiFi.softAPIP().toString().c_str(),
                  port,
                  getConfig()->webUser);
    if (isUsingDefaultPassword()) {
        Serial.println("[WEB] Please change the default web password from Settings");
    }
}

// ============================================================================
// Custom vehicle profile REST routes
// ============================================================================

void WebServerManager::_registerCustomVehicleRoutes() {
    // GET /api/vehicles/custom - list custom profiles (summary)
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
                item["id"]             = summary.id;
                item["name"]              = summary.name;
                item["brand"]               = summary.brand;
                item["model"]                  = summary.model;
                item["year"]                     = summary.year;
                item["commandCount"]                = summary.commandCount;
            }
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // POST /api/vehicles/custom/new - create a new empty profile
    // Form body: name, brand (optional), model (optional), year (optional)
    _server.on("/api/vehicles/custom/new", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        if (!_customStore) {
            request->send(500, "application/json", "{\"error\":\"ماژول پروفایل سفارشی مقداردهی نشده\"}");
            return;
        }

        String name    = request->arg("name");
        String brand      = request->arg("brand");
        String model         = request->arg("model");
        uint16_t year            = request->hasArg("year") ? request->arg("year").toInt() : 0;

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

    // POST /api/vehicles/custom/manual-add - add a manual command (SPEC section 5)
    // Form body: profileId, label, displayName, canId (hex string like "1A0"),
    //            extended ("1"/"0"), dataHex (e.g. "01 FF 00")
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

        uint8_t profileId    = request->arg("profileId").toInt();
        String  label            = request->arg("label");
        String  displayName          = request->hasArg("displayName") ? request->arg("displayName") : label;
        String  canIdHex                 = request->arg("canId");
        bool    extended                     = request->hasArg("extended") && request->arg("extended") == "1";
        String  dataHex                          = request->arg("dataHex");

        // Parse CAN ID (hex, with or without 0x prefix)
        uint32_t canId = strtoul(canIdHex.c_str(), nullptr, 16);
        uint32_t maxId = extended ? 0x1FFFFFFF : 0x7FF;
        if (canId > maxId) {
            request->send(400, "application/json",
                "{\"error\":\"CAN ID خارج از محدوده مجاز است\"}");
            return;
        }

        // Parse data bytes (space-separated hex, e.g. "01 FF 00")
        LearnedCommand cmd;
        strncpy(cmd.label, label.c_str(), sizeof(cmd.label) - 1);
        strncpy(cmd.displayName, displayName.c_str(), sizeof(cmd.displayName) - 1);
        cmd.canId          = canId;
        cmd.isExtended       = extended;
        cmd.source              = SOURCE_MANUAL;
        cmd.status                 = CMD_UNVERIFIED;  // Always starts unverified (SPEC section 5)
        cmd.timesObserved             = 0;
        cmd.failCount                    = 0;
        cmd.createdAt                       = millis();

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

    // GET /api/vehicles/custom/export?id=N - download a profile as JSON
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

    // POST /api/vehicles/custom/import - upload a profile JSON
    // Body: raw JSON in the "json" arg (urlencoded form) - kept simple
    // on ESPAsyncWebServer without needing multipart/file upload.
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

    // POST /api/vehicles/custom/delete - delete a profile
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

    // POST /api/vehicles/custom/select - select a custom profile as the active vehicle
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

// ============================================================================
// update()
// ============================================================================

void WebServerManager::update() {
    // Periodic cleanup of disconnected clients is optional - AsyncWebSocket
    // already fires WS_EVT_DISCONNECT, which we handle there.

    // Reboot after a successful OTA (a short delay lets the HTTP response
    // reach the browser first)
    if (_rebootPending && (int32_t)(millis() - _rebootAt) >= 0) {
        Serial.println("[OTA] Rebooting to apply update...");
        delay(100);
        ESP.restart();
    }
}

// ============================================================================
// OTA: file upload
// ============================================================================

void WebServerManager::_handleOtaUpload(AsyncWebServerRequest* request, const String& filename,
                                        size_t index, uint8_t* data, size_t len, bool final) {
    AppConfig* cfg = getConfig();
    // Nothing is written to flash without authentication (the 401
    // response itself is sent from _handleOtaFinished)
    if (!request->authenticate(cfg->webUser, cfg->webPass)) return;

    if (index == 0) {
        _otaError = "";
        _otaBytes = 0;
        _otaIsFs  = request->hasParam("type") && request->getParam("type")->value() == "fs";

        String lower = filename;
        lower.toLowerCase();
        if (!lower.endsWith(".bin")) {
            _otaError = "فایل باید با پسوند .bin باشد";
            return;
        }
        // Guard against mixing up firmware/filesystem/bootloader files
        bool looksBootloader = lower.indexOf("bootloader") >= 0 || lower.indexOf("partitions") >= 0;
        if (_otaIsFs) {
            if (looksBootloader || lower.indexOf("firmware") >= 0) {
                _otaError = "این فایل مربوط به فایل‌سیستم نیست (spiffs.bin را انتخاب کنید)";
                return;
            }
        } else {
            if (looksBootloader || lower.indexOf("spiffs") >= 0) {
                _otaError = "این فایل فریمویر نیست (firmware.bin را انتخاب کنید)";
                return;
            }
        }

        if (Update.isRunning()) Update.abort();
        if (_otaIsFs) SPIFFS.end();   // Before writing to the filesystem partition

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, _otaIsFs ? U_SPIFFS : U_FLASH)) {
            _otaError = String("شروع به‌روزرسانی ممکن نشد: ") + Update.errorString();
            return;
        }
        Serial.printf("[OTA] Starting: %s (%s)\n", filename.c_str(), _otaIsFs ? "filesystem" : "firmware");
    }

    if (_otaError.length() > 0) return;

    if (len > 0) {
        if (Update.write(data, len) != len) {
            _otaError = String("خطا در نوشتن: ") + Update.errorString();
            Update.abort();
            return;
        }
        _otaBytes += len;
    }

    if (final) {
        if (!Update.end(true)) {
            _otaError = String("پایان به‌روزرسانی ناموفق: ") + Update.errorString();
        } else {
            Serial.printf("[OTA] Finished successfully: %u bytes\n", (unsigned)_otaBytes);
        }
    }
}

// ============================================================================
// OTA: final response
// ============================================================================

void WebServerManager::_handleOtaFinished(AsyncWebServerRequest* request) {
    if (!_authenticate(request)) return;

    bool ok = (_otaError.length() == 0 && _otaBytes > 0 && !Update.hasError());

    if (ok) {
        _rebootPending = true;
        _rebootAt = millis() + 2000;
        request->send(200, "application/json",
            "{\"ok\":true,\"msg\":\"نصب شد. برد تا چند ثانیه‌ی دیگر ریست می‌شود؛ بعد از حدود ۱۵ ثانیه دوباره به وای‌فای CarTouch وصل شوید.\"}");
    } else {
        String err = _otaError;
        if (err.length() == 0) {
            err = (_otaBytes == 0) ? "فایلی دریافت نشد" : "خطای نامشخص";
        }
        if (Update.isRunning()) Update.abort();
        if (_otaIsFs) SPIFFS.begin(false);   // Remount so the web server keeps working
        Serial.printf("[OTA] Failed: %s\n", err.c_str());
        request->send(400, "application/json", "{\"ok\":false,\"msg\":\"" + err + "\"}");
    }

    _otaError = "";
    _otaBytes = 0;
}

// ============================================================================
// Command callback
// ============================================================================

void WebServerManager::setCommandCallback(WebCommandCallback cb) {
    _commandCallback = cb;
}

// ============================================================================
// Learn Mode state -> JSON
// ============================================================================

String WebServerManager::_learnStateToJSON() {
    if (!_learnEngine) return "{\"type\":\"learn_state\",\"state\":\"unavailable\"}";

    JsonDocument doc;
    doc["type"] = "learn_state";

    LearnModeState state = _learnEngine->getState();
    const char* stateStr = "idle";
    switch (state) {
        case LEARN_IDLE:              stateStr = "idle";               break;
        case LEARN_BASELINE_CAPTURE:  stateStr = "baseline_capture";   break;
        case LEARN_WAITING_ACTION:    stateStr = "waiting_action";     break;
        case LEARN_ACTION_CAPTURE:    stateStr = "action_capture";     break;
        case LEARN_CANDIDATES_READY:  stateStr = "candidates_ready";   break;
        case LEARN_ERROR:             stateStr = "error";              break;
    }
    doc["state"]         = stateStr;
    doc["progress"]         = _learnEngine->getProgressPercent();
    doc["label"]                = _learnEngine->getCurrentLabel();
    doc["displayName"]              = _learnEngine->getCurrentDisplayName();

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

            item["length"]      = c.length;
            item["isNew"]          = c.isNewMessage;
            item["seenCount"]         = c.seenCountInAction;
        }
    }

    String json;
    serializeJson(doc, json);
    return json;
}

// ============================================================================
// Learn Mode WebSocket messages
// ============================================================================
// Only called from _handleWebSocketEvent after auth->authenticated is
// confirmed - the same access level as regular "command" messages.

void WebServerManager::_handleLearnModeMessage(AsyncWebSocketClient* client, JsonDocument& doc, const char* type) {
    if (!_learnEngine || !_customStore || !_profileManager || !_vehicleControl) {
        client->printf("{\"type\":\"learn_error\",\"message\":\"ماژول‌های Learn Mode مقداردهی نشده‌اند\"}");
        return;
    }

    if (strcmp(type, "learn_start") == 0) {
        const char* label       = doc["label"] | "";
        const char* displayName    = doc["displayName"] | label;
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
        // User signals "pressing the button now" - starts the action capture window
        _learnEngine->confirmReadyForAction();
        client->text(_learnStateToJSON());

    } else if (strcmp(type, "learn_get_state") == 0) {
        // For polling progress (baseline/action capture is time-based)
        client->text(_learnStateToJSON());

    } else if (strcmp(type, "learn_save") == 0) {
        uint8_t candidateIndex = doc["candidateIndex"] | 255;
        uint8_t profileId         = doc["profileId"] | 255;

        LearnCandidate candidate;
        if (!_learnEngine->getCandidate(candidateIndex, candidate)) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"کاندید نامعتبر\"}");
            return;
        }

        LearnedCommand cmd;
        strncpy(cmd.label, _learnEngine->getCurrentLabel(), sizeof(cmd.label) - 1);
        strncpy(cmd.displayName, _learnEngine->getCurrentDisplayName(), sizeof(cmd.displayName) - 1);
        cmd.canId          = candidate.canId;
        cmd.isExtended       = candidate.isExtended;
        cmd.length              = candidate.length;
        memcpy(cmd.data, candidate.data, candidate.length);
        cmd.source                 = SOURCE_LEARNED;
        cmd.status                    = CMD_UNVERIFIED;  // Always starts unverified (SPEC section 4.2)
        cmd.timesObserved                = candidate.seenCountInAction;
        cmd.failCount                       = 0;
        cmd.createdAt                          = millis();

        bool ok = _customStore->upsertCommand(profileId, cmd);
        if (ok) {
            client->printf("{\"type\":\"learn_saved\",\"success\":true,\"label\":\"%s\"}", cmd.label);
            _learnEngine->cancel();  // Back to IDLE, ready for the next learn cycle
        } else {
            client->printf("{\"type\":\"learn_saved\",\"success\":false}");
        }

    } else if (strcmp(type, "learn_cancel") == 0) {
        _learnEngine->cancel();
        client->text(_learnStateToJSON());

    } else if (strcmp(type, "verify_command") == 0) {
        // Verification step (SPEC section 4.2): the user wants to
        // one-shot test an UNVERIFIED command. This is the only place a
        // learned/manual command can actually be sent on the bus - and
        // only via this explicit, confirmed message.
        uint8_t profileId  = doc["vehicleId"] | 255;
        const char* label     = doc["label"] | "";

        LearnedCommand cmd;
        if (!_customStore->findCommand(profileId, label, cmd)) {
            client->printf("{\"type\":\"verify_error\",\"message\":\"فرمان یافت نشد\"}");
            return;
        }

        // Real test send - goes through the same official path with rate-limiting
        _profileManager->selectCustomVehicle(profileId);
        String errReason;
        bool sent = _vehicleControl->executeCommand(label, errReason);

        JsonDocument respDoc;
        respDoc["type"]    = "verify_sent";
        respDoc["success"]    = sent;
        respDoc["canId"]         = cmd.canId;
        char hexId[12];
        snprintf(hexId, sizeof(hexId), "0x%03X", cmd.canId);
        respDoc["canIdHex"] = hexId;
        if (!sent) respDoc["error"] = errReason;

        String resp;
        serializeJson(respDoc, resp);
        client->text(resp);

    } else if (strcmp(type, "verify_confirm") == 0) {
        // After verify_command, the user manually confirms whether the
        // vehicle reacted correctly.
        uint8_t profileId  = doc["vehicleId"] | 255;
        const char* label     = doc["label"] | "";
        bool success             = doc["success"] | false;

        bool ok = _customStore->setCommandStatus(profileId, label,
                     success ? CMD_VERIFIED : CMD_UNVERIFIED, !success);

        client->printf("{\"type\":\"verify_confirmed\",\"success\":%s}", ok ? "true" : "false");
    }
}

// ============================================================================
// WebSocket events
// ============================================================================

void WebServerManager::_handleWebSocketEvent(AsyncWebSocket* server,
                                              AsyncWebSocketClient* client,
                                              AwsEventType type,
                                              void* arg,
                                              uint8_t* data,
                                              size_t len) {
    switch (type) {
        case WS_EVT_CONNECT: {
            Serial.printf("[WEB] Client %d connected (awaiting auth)\n", client->id());
            _findOrCreateClientAuth(client->id());
            client->printf("{\"type\":\"need_auth\"}");
            break;
        }

        case WS_EVT_DISCONNECT:
            Serial.printf("[WEB] Client %d disconnected\n", client->id());
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
                        Serial.printf("[WEB] Client %d authenticated\n", client->id());
                    } else {
                        client->printf("{\"type\":\"auth_failed\"}");
                        Serial.printf("[WEB] Failed auth attempt from client %d\n", client->id());
                        client->close(1008, "auth failed");
                    }
                    break;
                }

                if (!auth->authenticated) {
                    Serial.printf("[WEB] Unauthenticated message from client %d rejected\n", client->id());
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
                    // The same rate limit as regular commands also
                    // applies to verify_command, since that message can
                    // trigger a real send on the bus.
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

// ============================================================================
// HTTP authentication
// ============================================================================

bool WebServerManager::_authenticate(AsyncWebServerRequest* request) {
    AppConfig* cfg = getConfig();

    if (!request->authenticate(cfg->webUser, cfg->webPass)) {
        request->requestAuthentication("CarTouch");
        return false;
    }
    return true;
}

// ============================================================================
// Control API
// ============================================================================

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

// ============================================================================
// Status API
// ============================================================================

void WebServerManager::_handleAPIStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["status"]      = "ok";
    doc["message"]        = "CarTouch active";
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

// ============================================================================
// 404
// ============================================================================

void WebServerManager::_handleNotFound(AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "404 - Not Found");
}

// ============================================================================
// Broadcasts
// ============================================================================

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

// ============================================================================
// VehicleData -> JSON
// ============================================================================

String WebServerManager::_vehicleDataToJSON(const VehicleData& data) {
    JsonDocument doc;

    doc["type"]           = "vehicle_data";
    doc["speed"]              = data.vehicleSpeed;
    doc["rpm"]                    = data.engineRPM;
    doc["coolantTemp"]                = data.coolantTemp;
    doc["battery"]                       = data.batteryVoltage;
    doc["fuel"]                              = data.fuelLevel;
    doc["throttle"]                              = data.throttlePos;

    doc["doorFL"]   = (int)data.doorFL;
    doc["doorFR"]      = (int)data.doorFR;
    doc["doorRL"]         = (int)data.doorRL;
    doc["doorRR"]            = (int)data.doorRR;
    doc["trunk"]                 = (int)data.trunkState;
    doc["alarm"]                     = (int)data.alarmState;

    String output;
    serializeJson(doc, output);
    return output;
}

// ============================================================================
// Client status
// ============================================================================

bool WebServerManager::isClientConnected() {
    return _ws.count() > 0;
}

uint8_t WebServerManager::getClientCount() {
    return _ws.count();
}
