/**
 * webserver.cpp - Web server and WebSocket implementation
 *
 * Session-token auth, rate limiting, and auth on static files apply
 * uniformly across every route, including Learn Mode's messages/routes.
 *
 * User-facing strings (HTML page text and JSON error messages shown in the
 * UI) are English. Protocol identifiers remain stable and are not localized.
 */

#include "webserver.h"
#include "ble_manager.h"
#include "custom_vehicle.h"
#include "error_log.h"
#include "ct_verify.h"
#include <SPIFFS.h>
#include <esp_random.h>
#include <Update.h>

#include "ct_hex_parser.h"
#include "ct_index_parser.h"
#include "ct_battery.h"

static bool parseHexUint32(const char* text, uint32_t& value, size_t maxDigits) {
    return ctParseHexUint32(text, value, maxDigits);
}

static bool parseHexByteToken(const char* token, uint8_t& value) {
    return ctParseHexByteToken(token, value);
}

static bool parseHttpProfileIndex(const String& text, uint8_t& index) {
    return ctParseBoundedIndex(text.c_str(), MAX_CUSTOM_VEHICLES, index);
}

static bool parseJsonBoundedIndex(JsonVariantConst value, uint8_t limit, uint8_t& index) {
    if (!value.is<int>()) return false;
    const int parsed = value.as<int>();
    if (parsed < 0 || parsed >= limit) return false;
    index = (uint8_t)parsed;
    return true;
}

static bool parseJsonProfileIndex(JsonVariantConst value, uint8_t& index) {
    return parseJsonBoundedIndex(value, MAX_CUSTOM_VEHICLES, index);
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ OTA page (embedded in firmware, independent of SPIFFS)
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

// Deliberately kept in firmware rather than SPIFFS: this page still
// works even if the web asset files are corrupted.
static const char OTA_PAGE_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CarTouch - Firmware Update</title>
<style>
body{font-family:system-ui,sans-serif;background:#0f1a30;color:#eee;margin:0;padding:16px;max-width:760px;margin:auto}
.card{background:#16213e;border-radius:10px;padding:16px;margin-bottom:14px}
h3{margin:0 0 10px}.muted{opacity:.75;font-size:.9rem}input[type=file]{width:100%;margin:10px 0}button{width:100%;padding:12px;border:0;border-radius:8px;background:#e94560;color:#fff;font-size:1rem}button:disabled{opacity:.5}progress{width:100%;height:14px;margin-top:8px}.msg{margin-top:8px;font-size:.9rem}.warn{color:#f5a623;font-size:.85rem}a{color:#7fb3ff}
</style></head><body>
<div class="card"><h3>Firmware Update</h3><p class="muted">Upload a firmware .bin file. Do not power off the device during the update.</p>
<input type="file" id="f-fw" accept=".bin"><button id="b-fw">Upload and Install Firmware</button>
<progress id="p-fw" value="0" max="100" hidden></progress><div class="msg" id="m-fw"></div></div>
<div class="card"><h3>Web Filesystem Update</h3><p class="warn">Warning: installing the filesystem image replaces the SPIFFS contents, including custom profiles stored there.</p>
<input type="file" id="f-fs" accept=".bin"><button id="b-fs">Upload and Install Web Files</button>
<progress id="p-fs" value="0" max="100" hidden></progress><div class="msg" id="m-fs"></div></div>
<p><a href="/">← Back to CarTouch</a></p>
<script>
function up(t){
  var f=document.getElementById('f-'+t).files[0],m=document.getElementById('m-'+t),p=document.getElementById('p-'+t),b=document.getElementById('b-'+t);
  if(!f){m.textContent='Select a .bin file first.';return;}
  var x=new XMLHttpRequest(),fd=new FormData(); fd.append('file',f,f.name); b.disabled=true;p.hidden=false;p.value=0;
  m.textContent='Uploading... Do not close this page or power off the device.';
  x.upload.onprogress=function(e){if(e.lengthComputable)p.value=e.loaded*100/e.total;};
  x.onload=function(){b.disabled=false;var r;try{r=JSON.parse(x.responseText);}catch(e){r={ok:false,msg:'Invalid response ('+x.status+')'};}m.textContent=(r.ok?'✓ ':'✗ ')+r.msg;};
  x.onerror=function(){b.disabled=false;m.textContent='✗ Connection lost.';};
  x.open('POST','/update?type='+t); x.send(fd);
}
document.getElementById('b-fw').onclick=function(){up('fw');};
document.getElementById('b-fs').onclick=function(){up('fs');};
</script></body></html>)rawliteral";

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Constructor
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

WebServerManager* WebServerManager::_instance = nullptr;

WebServerManager::WebServerManager()
    : _server(WEB_PORT), _ws("/ws") {
    _commandCallback      = nullptr;
    _started                = false;
    _sessionToken             = "";
    _sessionTokenIssuedAt        = 0;

    _loginFailCount           = 0;
    _loginLockoutUntil           = 0;

    _otaError          = "";
    _otaBytes            = 0;
    _otaIsFs               = false;
    _rebootPending            = false;
    _rebootAt                   = 0;

    _moduleStatus       = nullptr;
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

void WebServerManager::invalidatePendingVerificationForProfile(uint8_t profileId) {
    for (int i = 0; i < WS_MAX_CLIENTS; ++i) {
        if (_clientAuth[i].inUse && _clientAuth[i].hasPendingVerify &&
            _clientAuth[i].pendingVerifyProfileId == profileId) {
            _clientAuth[i].hasPendingVerify = false;
            _clientAuth[i].pendingVerifyFingerprint = 0;
        }
    }
}

void WebServerManager::invalidateAllSessions() {
    // 1. Invalidate the HTTP session token.
    _sessionToken = "";
    _sessionTokenIssuedAt = 0;

    // 2. Invalidate every connected WebSocket client's auth state so
    // already-connected clients cannot continue using stale credentials.
    // authenticated=true could still send control commands, since
    // _isValidSessionToken is only checked on the initial "auth"
    // message, not on every subsequent command.
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        _clientAuth[i].authenticated = false;
        _clientAuth[i].sessionToken = "";
        _clientAuth[i].hasPendingVerify      = false;
        _clientAuth[i].pendingVerifyProfileId = 255;
        memset(_clientAuth[i].pendingVerifyLabel, 0, sizeof(_clientAuth[i].pendingVerifyLabel));
        _clientAuth[i].pendingVerifyToken    = 0;
        _clientAuth[i].pendingVerifyAt       = 0;
            _clientAuth[i].pendingVerifyFingerprint = 0;
    }

    Serial.println("[WEB] All web sessions invalidated (password changed)");
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Learn Mode module wiring
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void WebServerManager::attachLearnModules(LearnEngine* learnEngine,
                                          CustomVehicleStore* customStore,
                                          ActiveProfileManager* profileManager,
                                          VehicleControl* vehicleControl) {
    _learnEngine     = learnEngine;
    _customStore      = customStore;
    _profileManager     = profileManager;
    _vehicleControl       = vehicleControl;
}

void WebServerManager::setModuleStatusManager(ModuleStatusManager* manager) {
    _moduleStatus = manager;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Session token
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

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

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Client auth record management
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

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
            _clientAuth[i].sessionToken = "";
            _clientAuth[i].hasPendingVerify      = false;
            _clientAuth[i].pendingVerifyProfileId = 255;
            memset(_clientAuth[i].pendingVerifyLabel, 0, sizeof(_clientAuth[i].pendingVerifyLabel));
            _clientAuth[i].pendingVerifyToken    = 0;
            _clientAuth[i].pendingVerifyAt       = 0;
            _clientAuth[i].pendingVerifyFingerprint = 0;
            return &_clientAuth[i];
        }
    }

    Serial.println("[WEB] WebSocket client capacity full - reusing slot 0");
    _clientAuth[0].inUse             = true;
    _clientAuth[0].clientId            = clientId;
    _clientAuth[0].authenticated         = false;
    _clientAuth[0].lastCommandTime          = 0;
    _clientAuth[0].sessionToken = "";
    _clientAuth[0].hasPendingVerify      = false;
    _clientAuth[0].pendingVerifyProfileId = 255;
    memset(_clientAuth[0].pendingVerifyLabel, 0, sizeof(_clientAuth[0].pendingVerifyLabel));
    _clientAuth[0].pendingVerifyToken    = 0;
    _clientAuth[0].pendingVerifyAt       = 0;
    _clientAuth[0].pendingVerifyFingerprint = 0;
    return &_clientAuth[0];
}

void WebServerManager::_removeClientAuth(uint32_t clientId) {
    WsClientAuth* c = _findClientAuth(clientId);
    if (c) {
        c->inUse           = false;
        c->authenticated      = false;
        c->sessionToken         = "";
        c->clientId              = 0;
        // Drop any in-flight verification transaction (P0.1 hardening):
        // a disconnected client must not leave a live pending verify that
        // could later be confirmed after the slot is reused.
        c->hasPendingVerify      = false;
        c->pendingVerifyProfileId = 255;
        memset(c->pendingVerifyLabel, 0, sizeof(c->pendingVerifyLabel));
        c->pendingVerifyToken    = 0;
        c->pendingVerifyAt       = 0;
        c->pendingVerifyFingerprint = 0;
    }
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ begin()
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

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
        // This route owns the authentication response so the browser
        // receives one consistent Basic-Auth challenge.
        AppConfig* authCfg = getConfig();

        uint32_t remainingMs;
        if (_isLoginLocked(remainingMs)) {
            AsyncWebServerResponse* response = request->beginResponse(429, "text/html; charset=utf-8",
                "<html><head><meta charset='utf-8'></head><body dir='ltr'><h3>Too many failed attempts</h3>"
                "<p>Too many failed login attempts. Please try again later.</p>"
                "</body></html>");
            response->addHeader("Retry-After", String(remainingMs / 1000 + 1));
            request->send(response);
            return;
        }

        if (!request->authenticate(authCfg->webUser, authCfg->webPass)) {
            _registerLoginFailure();
            AsyncWebServerResponse* response = request->beginResponse(401, "text/html; charset=utf-8",
                "<html><head><meta charset='utf-8'></head><body dir='ltr'><h3>Unauthorized</h3>"
                "<p>Enter the username and password in the browser login dialog. If it does not appear, reopen the page.</p>"
                "</body></html>");
            response->addHeader("WWW-Authenticate", "Basic realm=\"CarTouch\"");
            request->send(response);
            return;
        }
        _registerLoginSuccess();

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
                "<h1>CarTouch</h1><p>index.html was not found.</p>");
        }
        response->addHeader("Set-Cookie", "cartouch_session=" + _sessionToken + "; Path=/; HttpOnly; SameSite=Strict");
        request->send(response);
    });

    _server.on("/login", HTTP_POST, [this](AsyncWebServerRequest* request) {
        uint32_t remainingMs;
        if (_isLoginLocked(remainingMs)) {
            AsyncWebServerResponse* response = request->beginResponse(429, "text/html; charset=utf-8",
                "<html><head><meta charset='utf-8'></head><body dir='ltr'><h3>Too many failed attempts</h3>"
                "<p>Too many failed login attempts. Please try again later.</p>"
                "<a href='/'>Back</a></body></html>");
            response->addHeader("Retry-After", String(remainingMs / 1000 + 1));
            request->send(response);
            return;
        }

        String     user = request->arg("user");
        String     pass = request->arg("pass");
        AppConfig* cfg  = getConfig();

        if (user.equals(cfg->webUser) && pass.equals(cfg->webPass)) {
            _registerLoginSuccess();
            _sessionToken = _generateSessionToken();
            _sessionTokenIssuedAt = millis();

            AsyncWebServerResponse* response = request->beginResponse(302, "text/plain", "");
            response->addHeader("Location", "/");
            response->addHeader("Set-Cookie", "cartouch_session=" + _sessionToken + "; Path=/; HttpOnly; SameSite=Strict");
            request->send(response);
        } else {
            // Deliberately does not log the submitted username/password -
            // only that an attempt failed, for basic brute-force
            // visibility without storing credential-adjacent data.
            getErrorLog()->log(LOG_CAT_WEB, LOG_WARN, "Failed login attempt");
            _registerLoginFailure();
            request->send(401, "text/html; charset=utf-8", "<html><head><meta charset='utf-8'></head><body dir='ltr'><h3>Invalid username or password</h3><a href='/'>Back</a></body></html>");
        }
    });

    _server.on("/api/session-token", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
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
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }
        _handleAPIControl(request);
    });

    _server.on("/api/can-config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) return;
        _handleAPICanConfig(request);
    });

    _server.on("/api/change-password", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }

        String newUser     = request->arg("newUser");
        String newPass     = request->arg("newPass");
        String confirmPass = request->arg("confirmPass");

        if (newPass.length() == 0) {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"New password is empty\"}");
            return;
        }
        if (!newPass.equals(confirmPass)) {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Passwords do not match\"}");
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
                "{\"success\":false,\"error\":\"Password must be at least 8 characters and different from the default password\"}");
        }
    });

    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }
        _handleAPIStatus(request);
    });

    // Checklist item 16 (error logging/telemetry) - read-only, same
    // auth level as everything else. See error_log.h.
    _server.on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }
        request->send(200, "application/json", getErrorLog()->toJSON());
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

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Custom vehicle profile REST routes
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void WebServerManager::_registerCustomVehicleRoutes() {
    // GET /api/vehicles/custom - list custom profiles (summary)
    _server.on("/api/vehicles/custom", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }
        if (!_customStore) {
            request->send(500, "application/json", "{\"error\":\"Custom profile module is not initialized\"}");
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
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }
        if (!_customStore) {
            request->send(500, "application/json", "{\"error\":\"Custom profile module is not initialized\"}");
            return;
        }

        String   name  = request->arg("name");
        String   brand = request->arg("brand");
        String   model = request->arg("model");
        uint16_t year  = request->hasArg("year") ? request->arg("year").toInt() : 0;

        if (name.length() == 0) {
            request->send(400, "application/json", "{\"error\":\"Profile name is required\"}");
            return;
        }

        uint8_t newIndex;
        bool ok = _customStore->createNewProfile(name.c_str(), brand.c_str(),
                                                   model.c_str(), year, newIndex);
        if (ok) {
            String json = "{\"success\":true,\"id\":" + String(newIndex) + "}";
            request->send(200, "application/json", json);
        } else {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Profile capacity is full\"}");
        }
    });

    // POST /api/vehicles/custom/manual-add - add a manual command (see CarTouch_SPEC.md)
    // Form body: profileId, label, displayName, canId (hex string like "1A0"),
    //            extended ("1"/"0"), dataHex (e.g. "01 FF 00")
    _server.on("/api/vehicles/custom/manual-add", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }
        if (!_customStore) {
            request->send(500, "application/json", "{\"error\":\"Custom profile module is not initialized\"}");
            return;
        }

        if (!request->hasArg("profileId") || !request->hasArg("label") ||
            !request->hasArg("canId") || !request->hasArg("dataHex")) {
            request->send(400, "application/json", "{\"error\":\"Required fields are missing\"}");
            return;
        }

        uint8_t profileId;
        if (!parseHttpProfileIndex(request->arg("profileId"), profileId)) {
            request->send(400, "application/json", "{\"error\":\"Profile ID is invalid\"}");
            return;
        }
        String  label       = request->arg("label");
        String  displayName = request->hasArg("displayName") ? request->arg("displayName") : label;
        String  canIdHex    = request->arg("canId");
        bool    extended    = request->hasArg("extended") && request->arg("extended") == "1";
        String  dataHex     = request->arg("dataHex");

        // Parse CAN ID strictly; reject malformed text instead of accepting
        // only a fully valid hexadecimal token is accepted.
        uint32_t canId = 0;
        uint32_t maxId = extended ? 0x1FFFFFFF : 0x7FF;
        if (!parseHexUint32(canIdHex.c_str(), canId, 8) || canId > maxId) {
            request->send(400, "application/json",
                "{\"error\":\"CAN ID is invalid or out of range\"}");
            return;
        }

        // Parse data bytes (space-separated hex, e.g. "01 FF 00")
        LearnedCommand cmd;
        if (label.length() == 0 || label.length() >= sizeof(cmd.label) ||
            displayName.length() >= sizeof(cmd.displayName)) {
            request->send(400, "application/json",
                "{\"error\":\"Command label length is invalid\"}");
            return;
        }
        strncpy(cmd.label, label.c_str(), sizeof(cmd.label) - 1);
        cmd.label[sizeof(cmd.label) - 1] = '\0';
        strncpy(cmd.displayName, displayName.c_str(), sizeof(cmd.displayName) - 1);
        cmd.displayName[sizeof(cmd.displayName) - 1] = '\0';
        cmd.canId          = canId;
        cmd.isExtended       = extended;
        cmd.source              = SOURCE_MANUAL;
        cmd.status                 = CMD_UNVERIFIED;                                   // Always starts unverified
        cmd.timesObserved             = 0;
        cmd.failCount                    = 0;
        cmd.createdAt                       = millis();

        uint8_t len = 0;
        char buf[64];
        strncpy(buf, dataHex.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* token = strtok(buf, " \t\r\n");
        while (token) {
            if (len >= 8 || !parseHexByteToken(token, cmd.data[len])) {
                request->send(400, "application/json",
                    "{\"error\":\"CAN data must contain 1 to 8 valid hexadecimal bytes\"}");
                return;
            }
            ++len;
            token = strtok(nullptr, " \t\r\n");
        }
        if (len == 0) {
            request->send(400, "application/json",
                "{\"error\":\"Enter at least one data byte\"}");
            return;
        }
        cmd.length = len;

        bool ok = _customStore->upsertCommand(profileId, cmd);
        if (ok) {
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
        }
    });

    // GET /api/vehicles/custom/export?id=N - download a profile as JSON
    _server.on("/api/vehicles/custom/export", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }
        if (!_customStore || !request->hasArg("id")) {
            request->send(400, "application/json", "{\"error\":\"Profile ID is required\"}");
            return;
        }

        uint8_t id;
        if (!parseHttpProfileIndex(request->arg("id"), id)) {
            request->send(400, "application/json", "{\"error\":\"Profile ID is invalid\"}");
            return;
        }
        String json;
        if (!_customStore->exportProfileJSON(id, json)) {
            request->send(404, "application/json", "{\"error\":\"Profile not found\"}");
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
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }
        if (!_customStore || !request->hasArg("json")) {
            request->send(400, "application/json", "{\"error\":\"JSON content is required\"}");
            return;
        }

        if (request->arg("json").length() > MAX_IMPORT_JSON_LEN) {
            request->send(413, "application/json",
                          "{\"error\":\"Profile JSON exceeds the maximum accepted size\"}");
            return;
        }

        String jsonBody = request->arg("json");
        uint8_t newIndex;
        bool ok = _customStore->importProfileJSON(jsonBody, newIndex);
        if (ok) {
            String resp = "{\"success\":true,\"id\":" + String(newIndex) +
                         ",\"note\":\"All imported commands were marked unverified\"}";
            request->send(200, "application/json", resp);
        } else {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid file or profile capacity is full\"}");
        }
    });

    // POST /api/vehicles/custom/delete - delete a profile
    _server.on("/api/vehicles/custom/delete", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }
        if (!_customStore || !request->hasArg("id")) {
            request->send(400, "application/json", "{\"error\":\"Profile ID is required\"}");
            return;
        }

        uint8_t id;
        if (!parseHttpProfileIndex(request->arg("id"), id)) {
            request->send(400, "application/json", "{\"error\":\"Profile ID is invalid\"}");
            return;
        }
        invalidatePendingVerificationForProfile(id);
        bool    ok = _customStore->deleteProfile(id);
        if (ok && _profileManager) {
            // If the deleted profile happened to be the active vehicle, clear
            // the selection so a dangling index can never be resolved later.
            _profileManager->clearActiveIfCustom(id);
        }
        request->send(ok ? 200 : 400, "application/json",
                      ok ? "{\"success\":true}" : "{\"success\":false}");
    });

    // POST /api/vehicles/custom/select - select a custom profile as the active vehicle
    _server.on("/api/vehicles/custom/select", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_authenticate(request)) {
            // _authenticate() already sent the 401/429 response itself
            // (including the WWW-Authenticate challenge or Retry-After
            // header) - sending a second response here would double-send
            // on the same AsyncWebServerRequest, which is undefined
            // behavior in ESPAsyncWebServer (can crash the async task or
            // corrupt the connection).
            return;
        }
        if (!_profileManager || !request->hasArg("id")) {
            request->send(400, "application/json", "{\"error\":\"Profile ID is required\"}");
            return;
        }

        uint8_t id;
        if (!parseHttpProfileIndex(request->arg("id"), id)) {
            request->send(400, "application/json", "{\"error\":\"Profile ID is invalid\"}");
            return;
        }
        bool    ok = _profileManager->selectCustomVehicle(id);
        request->send(ok ? 200 : 404, "application/json",
                      ok ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Profile not found\"}");
    });
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ update()
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

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

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ OTA: file upload
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

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
            _otaError = "File must use the .bin extension";
            return;
        }
        // Guard against mixing up firmware/filesystem/bootloader files
        bool looksBootloader = lower.indexOf("bootloader") >= 0 || lower.indexOf("partitions") >= 0;
        if (_otaIsFs) {
            if (looksBootloader || lower.indexOf("firmware") >= 0) {
                _otaError = "This is not a filesystem image (select spiffs.bin)";
                return;
            }
        } else {
            if (looksBootloader || lower.indexOf("spiffs") >= 0) {
                _otaError = "This is not a firmware image (select firmware.bin)";
                return;
            }
        }

        if (Update.isRunning()) Update.abort();
        if (_otaIsFs) SPIFFS.end();                // Before writing to the filesystem partition

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, _otaIsFs ? U_SPIFFS : U_FLASH)) {
            _otaError = String("Failed to start update: ") + Update.errorString();
            return;
        }
        Serial.printf("[OTA] Starting: %s (%s)\n", filename.c_str(), _otaIsFs ? "filesystem" : "firmware");
    }

    if (_otaError.length() > 0) return;

    if (len > 0) {
        if (Update.write(data, len) != len) {
            _otaError = String("Write error: ") + Update.errorString();
            Update.abort();
            return;
        }
        _otaBytes += len;
    }

    if (final) {
        if (!Update.end(true)) {
            _otaError = String("Update finalization failed: ") + Update.errorString();
        } else {
            Serial.printf("[OTA] Finished successfully: %u bytes\n", (unsigned)_otaBytes);
        }
    }
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ OTA: final response
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void WebServerManager::_handleOtaFinished(AsyncWebServerRequest* request) {
    if (!_authenticate(request)) return;

    bool ok = (_otaError.length() == 0 && _otaBytes > 0 && !Update.hasError());

    if (ok) {
        _rebootPending = true;
        _rebootAt = millis() + 2000;
        request->send(200, "application/json",
            "{\"ok\":true,\"msg\":\"Installed. The device will reboot shortly; reconnect to the CarTouch Wi-Fi after about 15 seconds.\"}");
    } else {
        String err = _otaError;
        if (err.length() == 0) {
            err = (_otaBytes == 0) ? "No file received" : "Unknown error";
        }
        if (Update.isRunning()) Update.abort();
        if (_otaIsFs) SPIFFS.begin(false);                                                   // Remount so the web server keeps working
        Serial.printf("[OTA] Failed: %s\n", err.c_str());
        request->send(400, "application/json", "{\"ok\":false,\"msg\":\"" + err + "\"}");
    }

    _otaError = "";
    _otaBytes = 0;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Command callback
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void WebServerManager::setCommandCallback(WebCommandCallback cb) {
    _commandCallback = cb;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Learn Mode state -> JSON
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

String WebServerManager::_learnStateToJSON() {
    if (!_learnEngine) return "{\"type\":\"learn_state\",\"state\":\"unavailable\"}";

    JsonDocument doc;
    doc["type"] = "learn_state";

    LearnModeState state    = _learnEngine->getState();
    const char*    stateStr = "idle";
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
        uint8_t   count      = _learnEngine->getCandidateCount();
        for (int i = 0; i < count; i++) {
            LearnCandidate c;
            if (!_learnEngine->getCandidate(i, c)) continue;

            JsonObject item = candidates.add<JsonObject>();
            item["index"] = i;
            item["canId"] = c.canId;
            item["isExtended"] = c.isExtended;

            char hexId[12];
            snprintf(hexId, sizeof(hexId), c.isExtended ? "0x%08lX" : "0x%03lX",
                     (unsigned long)c.canId);
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

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Learn Mode WebSocket messages
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

// Only called from _handleWebSocketEvent after auth->authenticated is
// confirmed - the same access level as regular "command" messages.

void WebServerManager::_handleLearnModeMessage(AsyncWebSocketClient* client, JsonDocument& doc, const char* type) {
    if (!_learnEngine || !_customStore || !_profileManager || !_vehicleControl) {
        client->printf("{\"type\":\"learn_error\",\"message\":\"Learn Mode modules are not initialized\"}");
        return;
    }

    if (strcmp(type, "learn_start") == 0) {
        const char* label       = doc["label"] | "";
        const char* displayName = doc["displayName"] | label;
        if (strlen(label) == 0) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"Command label is required\"}");
            return;
        }
        uint8_t profileId;
        if (!parseJsonProfileIndex(doc["profileId"], profileId)) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"Target profile is invalid\"}");
            return;
        }
        CustomVehicleProfile targetProfile;
        if (!_customStore->getProfileSummary(profileId, targetProfile)) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"Target profile is invalid\"}");
            return;
        }
        _learnEngine->beginLearning(label, displayName, profileId);
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
        uint8_t candidateIndex;
        uint8_t profileId;
        if (!parseJsonBoundedIndex(doc["candidateIndex"], CANDIDATE_MAX, candidateIndex) ||
            !parseJsonProfileIndex(doc["profileId"], profileId)) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"Invalid profile or candidate index\"}");
            return;
        }

        // A profile may only be created from a *completed* learn cycle.
        // getCandidate() already fails outside CANDIDATES_READY because the
        // candidate count is reset to 0 there, but checking the state
        // explicitly makes the invariant impossible to break silently in a
        // future refactor.
        if (_learnEngine->getState() != LEARN_CANDIDATES_READY) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"no completed learn cycle\"}");
            return;
        }

        if (profileId != _learnEngine->getTargetProfileId()) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"Saved profile does not match the learning session profile\"}");
            return;
        }

        CustomVehicleProfile targetProfile;
        if (!_customStore->getProfileSummary(profileId, targetProfile)) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"Target profile no longer exists\"}");
            return;
        }

        LearnCandidate candidate;
        if (!_learnEngine->getCandidate(candidateIndex, candidate)) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"Invalid candidate\"}");
            return;
        }

        LearnedCommand cmd;
        strncpy(cmd.label, _learnEngine->getCurrentLabel(), sizeof(cmd.label) - 1);
        cmd.label[sizeof(cmd.label) - 1] = '\0';    // strncpy may not NUL-terminate on truncation
        strncpy(cmd.displayName, _learnEngine->getCurrentDisplayName(), sizeof(cmd.displayName) - 1);
        cmd.displayName[sizeof(cmd.displayName) - 1] = '\0';

        // The label is the command's unique key inside the profile
        // store and the payload must fit the fixed 8-byte frame, so
        // reject both before anything is written to the store.
        if (cmd.label[0] == '\0' || candidate.length == 0 ||
            candidate.length > sizeof(cmd.data)) {
            client->printf("{\"type\":\"learn_error\",\"message\":\"Invalid candidate\"}");
            return;
        }

        cmd.canId          = candidate.canId;
        cmd.isExtended       = candidate.isExtended;
        cmd.length              = candidate.length;
        memcpy(cmd.data, candidate.data, candidate.length);
        cmd.source                 = SOURCE_LEARNED;
        cmd.status                    = CMD_UNVERIFIED;                                                  // Always starts unverified
        cmd.timesObserved                = candidate.seenCountInAction;
        cmd.failCount                       = 0;
        cmd.createdAt                          = millis();

        bool ok = _customStore->upsertCommand(profileId, cmd);
        if (ok) {
            JsonDocument savedDoc;
            savedDoc["type"] = "learn_saved";
            savedDoc["success"] = true;
            savedDoc["label"] = cmd.label;
            String savedJson;
            serializeJson(savedDoc, savedJson);
            client->text(savedJson);
            _learnEngine->cancel();                                                                       // Back to IDLE, ready for the next learn cycle
        } else {
            client->printf("{\"type\":\"learn_saved\",\"success\":false}");
        }

    } else if (strcmp(type, "learn_cancel") == 0) {
        _learnEngine->cancel();
        client->text(_learnStateToJSON());

    } else if (strcmp(type, "verify_command") == 0) {
        // Verification step (see README.md): the user wants to
        // one-shot test an UNVERIFIED command. executeCommandForVerification()
        // is the only entry point that lets an UNVERIFIED command
        // through - regular command dispatch always rejects it.
        uint8_t profileId;
        if (!parseJsonProfileIndex(doc["vehicleId"], profileId)) {
            client->printf("{\"type\":\"verify_error\",\"message\":\"Command profile is invalid\"}");
            return;
        }
        const char* label     = doc["label"] | "";

        LearnedCommand cmd;
        CustomVehicleProfile cmdProfile;
        if (!_customStore->findCommand(profileId, label, cmd) ||
            !_customStore->getProfileSummary(profileId, cmdProfile)) {
            client->printf("{\"type\":\"verify_error\",\"message\":\"Command not found\"}");
            return;
        }

        // Real test send - goes through the same rate-limit/duty-cycle/
        // Listen-Only checks as any other command, via the dedicated
        // verification entry point. Never continue with the previously
        // active profile if the requested profile cannot be selected.
        if (!_profileManager->selectCustomVehicle(profileId)) {
            client->printf("{\"type\":\"verify_error\",\"message\":\"Profile selection failed\"}");
            return;
        }
        String errReason;
        bool sent = _vehicleControl->executeCommandForVerification(label, errReason);

        // Open a verification transaction ONLY for a send that actually
        // happened. verify_confirm is then required to present this
        // transaction's token, from the same client, for the same profile
        // and label - so an arbitrary or replayed confirmation cannot
        // change a command's status.
        uint32_t token = 0;
        WsClientAuth* vAuth = _findClientAuth(client->id());
        if (vAuth) {
            if (sent) {
                token = esp_random();
                vAuth->hasPendingVerify       = true;
                vAuth->pendingVerifyProfileId = profileId;
                memset(vAuth->pendingVerifyLabel, 0, sizeof(vAuth->pendingVerifyLabel));
                strncpy(vAuth->pendingVerifyLabel, label,
                        sizeof(vAuth->pendingVerifyLabel) - 1);
                vAuth->pendingVerifyToken     = token;
                vAuth->pendingVerifyAt        = millis();
                vAuth->pendingVerifyFingerprint = ctVerifyFingerprint(
                    profileId, cmdProfile.revision, cmd.label, cmd.canId, cmd.isExtended, cmd.length, cmd.data);
            } else {
                // A failed send invalidates any earlier pending transaction.
                vAuth->hasPendingVerify = false;
                vAuth->pendingVerifyFingerprint = 0;
            }
        }

        JsonDocument respDoc;
        respDoc["type"]    = "verify_sent";
        respDoc["success"]    = sent;
        respDoc["canId"]         = cmd.canId;
        char hexId[12];
        snprintf(hexId, sizeof(hexId), "0x%03X", cmd.canId);
        respDoc["canIdHex"] = hexId;
        if (sent) respDoc["verifyToken"] = token;
        if (!sent) respDoc["error"] = errReason;

        String resp;
        serializeJson(respDoc, resp);
        client->text(resp);

    } else if (strcmp(type, "verify_confirm") == 0) {
        // After verify_command, the user manually confirms whether the
        // vehicle reacted correctly. Accepted ONLY when it matches the
        // verification transaction verify_command opened for THIS
        // client: same profile, same label, matching token, and within
        // VERIFY_PENDING_TIMEOUT_MS. Consumed on use, so it cannot be
        // replayed, and an arbitrary confirmation is rejected.
        uint8_t profileId;
        if (!parseJsonProfileIndex(doc["vehicleId"], profileId)) {
            client->printf("{\"type\":\"verify_confirmed\",\"success\":false,\"message\":\"Command profile is invalid\"}");
            return;
        }
        const char* label     = doc["label"] | "";
        bool        success   = doc["success"] | false;
        uint32_t    token     = doc["verifyToken"] | 0;

        WsClientAuth* vAuth = _findClientAuth(client->id());
        if (!vAuth) {
            client->printf("{\"type\":\"verify_confirmed\",\"success\":false,"
                           "\"message\":\"\u0646\u0634\u0633\u062a \u0646\u0627\u0645\u0639\u062a\u0628\u0631 \u0627\u0633\u062a\"}");
            return;
        }

        if (!ctVerifyTransactionValid(
                vAuth->hasPendingVerify, vAuth->pendingVerifyProfileId,
                vAuth->pendingVerifyLabel, vAuth->pendingVerifyToken,
                vAuth->pendingVerifyAt, profileId, label, token, millis(),
                VERIFY_PENDING_TIMEOUT_MS)) {
            vAuth->hasPendingVerify = false;
            vAuth->pendingVerifyFingerprint = 0;
            client->printf("{\"type\":\"verify_confirmed\",\"success\":false,\"message\":\"Verification is invalid or expired\"}");
            return;
        }

        // Re-read the command at confirmation time. This closes the residual
        // race where a command could be edited/relearned/deleted after the
        // test-send but before confirmation. A confirmation is valid only
        // for the exact payload that was actually sent and only while the
        // command remains UNVERIFIED in the same profile.
        LearnedCommand currentCmd;
        CustomVehicleProfile currentProfile;
        if (!_customStore->findCommand(profileId, label, currentCmd) ||
            !_customStore->getProfileSummary(profileId, currentProfile) ||
            currentCmd.status != CMD_UNVERIFIED ||
            ctVerifyFingerprint(profileId, currentProfile.revision, currentCmd.label, currentCmd.canId,
                                currentCmd.isExtended, currentCmd.length, currentCmd.data) !=
                vAuth->pendingVerifyFingerprint) {
            vAuth->hasPendingVerify = false;
            vAuth->pendingVerifyFingerprint = 0;
            client->printf("{\"type\":\"verify_confirmed\",\"success\":false,\"message\":\"Command or profile changed since the test\"}");
            return;
        }

        // Consume the transaction before writing, so a replayed message
        // cannot apply twice.
        vAuth->hasPendingVerify = false;
        vAuth->pendingVerifyFingerprint = 0;

        bool ok = _customStore->setCommandStatus(profileId, label,
                     success ? CMD_VERIFIED : CMD_UNVERIFIED, !success);

        client->printf("{\"type\":\"verify_confirmed\",\"success\":%s}", ok ? "true" : "false");
    }
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ WebSocket events
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

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

            // Bound the memory a single peer can make the device buffer
            // before any JSON parsing or authentication runs. 1009 is the
            // WebSocket "message too big" close code.
            if (info->len > WS_MAX_MESSAGE_LEN) {
                Serial.printf("[WEB] Oversized WS message from client %d (%u bytes) - closing\n",
                              client->id(), (unsigned)info->len);
                client->close(1009, "message too large");
                break;
            }

            if (info->final && info->index == 0 && info->len == len) {
                String msg;
                msg.reserve(len + 1);
                msg.concat(reinterpret_cast<const char*>(data), len);

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
                        auth->sessionToken = token;
                        client->printf("{\"type\":\"welcome\",\"message\":\"Welcome to CarTouch\"}");
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

                // WebSocket authentication is bound to the same finite
                // lifetime as the HTTP session token. Re-check it for every
                // post-auth message so an already-connected client cannot
                // continue issuing commands after the session expires or
                // after a password change invalidates the token.
                if (!_isValidSessionToken(auth->sessionToken.c_str())) {
                    auth->authenticated = false;
                    auth->sessionToken = "";
                    client->printf("{\"type\":\"session_expired\"}");
                    Serial.printf("[WEB] Session expired for client %d\n", client->id());
                    client->close(1008, "session expired");
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
                        JsonDocument ack;
                        ack["type"] = "ack";
                        ack["command"] = command;
                        String ackJson;
                        serializeJson(ack, ackJson);
                        client->text(ackJson);
                    }
                } else if (strcmp(msgType, "ping") == 0) {
                    client->printf("{\"type\":\"pong\"}");
                } else if (strcmp(msgType, "get_logs") == 0) {
                    // Same data as GET /api/logs, available over the
                    // existing authenticated WebSocket connection too
                    // (checklist item 16 - error logging/telemetry).
                    client->text(getErrorLog()->toJSON());
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

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ HTTP authentication
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

//
// Brute-force protection:
// _isLoginLocked() must be checked by every entry point that directly
// compares a submitted password against cfg->webPass, BEFORE that
// comparison happens - never after. Checking after the comparison would
// still let an attacker distinguish "right password, locked" from
// "wrong password" via timing/behavior, and would do the expensive
// comparison work on every locked-out attempt for no reason.
//
// _registerLoginFailure() must be called on every failed comparison,
// and _registerLoginSuccess() on every successful one, at all three
// call sites ("/", "/login", _authenticate()) - they all share the same
// counter/lockout state, so a lockout triggered via one path (e.g.
// repeated bad Basic-Auth on "/") also blocks the others (e.g. "/login")
// until it expires.

bool WebServerManager::_isLoginLocked(uint32_t& remainingMs) {
    if (_loginLockoutUntil == 0) return false;
    uint32_t now = millis();
    // Wrap-safe elapsed check: the lockout window (30 s) is far shorter
    // than the 32-bit millis() period (~49 days), so the signed difference
    // is authoritative even when millis() rolls over mid-lockout.
    if ((int32_t)(now - _loginLockoutUntil) >= 0) {
        // Lockout window has passed - reset and let a fresh attempt through
        _loginLockoutUntil = 0;
        _loginFailCount = 0;
        return false;
    }
    remainingMs = _loginLockoutUntil - now;
    return true;
}

void WebServerManager::_registerLoginFailure() {
    if (_loginFailCount < 255) _loginFailCount++;
    if (_loginFailCount >= LOGIN_MAX_ATTEMPTS) {
        uint32_t until = millis() + LOGIN_LOCKOUT_MS;
        // 0 is the "not locked" sentinel; avoid a (rare) collision when the
        // computed end timestamp lands exactly on 0.
        _loginLockoutUntil = (until == 0) ? 1 : until;
        getErrorLog()->log(LOG_CAT_WEB, LOG_WARN,
            "Login lockout triggered after %u failed attempts", _loginFailCount);
    }
}

void WebServerManager::_registerLoginSuccess() {
    _loginFailCount = 0;
    _loginLockoutUntil = 0;
}

// Contract: on failure (locked out or bad credentials), _authenticate()
// itself sends the appropriate response to `request` (429+Retry-After,
// or 401+WWW-Authenticate via requestAuthentication()) and returns
// false. Callers must NOT send another response in that case - do
// exactly `if (!_authenticate(request)) return;` with nothing else.
// Sending twice on the same AsyncWebServerRequest is undefined
// behavior in ESPAsyncWebServer.
bool WebServerManager::_authenticate(AsyncWebServerRequest* request) {
    AppConfig* cfg = getConfig();

    uint32_t remainingMs;
    if (_isLoginLocked(remainingMs)) {
        AsyncWebServerResponse* response = request->beginResponse(429, "application/json",
            "{\"error\":\"Too many failed login attempts. Try again later.\"}");
        response->addHeader("Retry-After", String(remainingMs / 1000 + 1));
        request->send(response);
        return false;
    }

    if (!request->authenticate(cfg->webUser, cfg->webPass)) {
        getErrorLog()->log(LOG_CAT_WEB, LOG_WARN, "Failed basic-auth attempt (%s)", request->url().c_str());
        _registerLoginFailure();
        request->requestAuthentication("CarTouch");
        return false;
    }
    _registerLoginSuccess();
    return true;
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Control API
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void WebServerManager::_handleAPIControl(AsyncWebServerRequest* request) {
    String command = request->arg("command");

    if (command.length() == 0) {
        request->send(400, "application/json", "{\"error\":\"command parameter required\"}");
        return;
    }

    if (_commandCallback) {
        _commandCallback(command.c_str());
    }

    JsonDocument doc;
    doc["success"] = true;
    doc["command"] = command;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void WebServerManager::_handleAPICanConfig(AsyncWebServerRequest* request) {
    if (!request->hasArg("txPin") || !request->hasArg("rxPin") ||
        !request->hasArg("speed") || !request->hasArg("listenOnly")) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"txPin, rxPin, speed and listenOnly are required\"}");
        return;
    }

    const bool hasCan1Cs = request->hasArg("can1CsPin");
    const bool hasCan1Int = request->hasArg("can1IntPin");
    const bool hasCan1Speed = request->hasArg("can1Speed");
    const bool hasCan1ListenOnly = request->hasArg("can1ListenOnly");
    const bool hasAnyCan1 = hasCan1Cs || hasCan1Int || hasCan1Speed || hasCan1ListenOnly;
    const bool hasAllCan1 = hasCan1Cs && hasCan1Int && hasCan1Speed && hasCan1ListenOnly;
    if (hasAnyCan1 && !hasAllCan1) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"All CAN1 settings are required together\"}");
        return;
    }

    const String txArg = request->arg("txPin");
    const String rxArg = request->arg("rxPin");
    const String speedArg = request->arg("speed");
    const String listenOnlyArg = request->arg("listenOnly");
    AppConfig* cfg = getConfig();
    uint8_t tx = 0;
    uint8_t rx = 0;
    uint8_t can1CsPin = cfg->can1CsPin;
    uint8_t can1IntPin = cfg->can1IntPin;
    uint32_t speed = 0;
    uint32_t can1Speed = cfg->can1Speed;
    bool listenOnly = false;
    bool can1ListenOnly = cfg->can1ListenOnly;

    const bool can0Valid = ctParseBoundedIndex(txArg.c_str(), 49, tx) &&
                           ctParseBoundedIndex(rxArg.c_str(), 49, rx) &&
                           ctParseUnsignedDecimal(speedArg.c_str(), 1000000u, speed) &&
                           ctParseBoolean(listenOnlyArg.c_str(), listenOnly) &&
                           isValidCanSpeed(speed);
    bool can1Valid = true;
    if (hasAllCan1) {
        const String can1CsArg = request->arg("can1CsPin");
        const String can1IntArg = request->arg("can1IntPin");
        const String can1SpeedArg = request->arg("can1Speed");
        const String can1ListenOnlyArg = request->arg("can1ListenOnly");
        can1Valid = ctParseBoundedIndex(can1CsArg.c_str(), 49, can1CsPin) &&
                    ctParseBoundedIndex(can1IntArg.c_str(), 49, can1IntPin) &&
                    ctParseUnsignedDecimal(can1SpeedArg.c_str(), 1000000u, can1Speed) &&
                    ctParseBoolean(can1ListenOnlyArg.c_str(), can1ListenOnly) &&
                    isValidCan1Speed(can1Speed);
    }

    if (!can0Valid || !can1Valid ||
        !validateCanPinAssignment(tx, rx, can1CsPin, can1IntPin)) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid or conflicting CAN configuration\"}");
        return;
    }

    const uint8_t oldTxPin = cfg->canTxPin;
    const uint8_t oldRxPin = cfg->canRxPin;
    const uint32_t oldSpeed = cfg->canSpeed;
    const bool oldListenOnly = cfg->listenOnlyMode;
    const uint8_t oldCan1CsPin = cfg->can1CsPin;
    const uint8_t oldCan1IntPin = cfg->can1IntPin;
    const uint32_t oldCan1Speed = cfg->can1Speed;
    const bool oldCan1ListenOnly = cfg->can1ListenOnly;
    cfg->canTxPin = tx;
    cfg->canRxPin = rx;
    cfg->canSpeed = speed;
    cfg->listenOnlyMode = listenOnly;
    cfg->can1CsPin = can1CsPin;
    cfg->can1IntPin = can1IntPin;
    cfg->can1Speed = can1Speed;
    cfg->can1ListenOnly = can1ListenOnly;

    if (!saveConfig()) {
        cfg->canTxPin = oldTxPin;
        cfg->canRxPin = oldRxPin;
        cfg->canSpeed = oldSpeed;
        cfg->listenOnlyMode = oldListenOnly;
        cfg->can1CsPin = oldCan1CsPin;
        cfg->can1IntPin = oldCan1IntPin;
        cfg->can1Speed = oldCan1Speed;
        cfg->can1ListenOnly = oldCan1ListenOnly;
        request->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to save CAN configuration\"}");
        return;
    }

    JsonDocument doc;
    doc["success"] = true;
    doc["rebootRequired"] = true;
    doc["message"] = "CAN configuration saved. The device will reboot to apply it.";
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
    _rebootPending = true;
    _rebootAt = millis() + 1500;
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Status API
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

void WebServerManager::_handleAPIStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["status"]      = "ok";
    doc["message"]     = "CarTouch active";
    doc["usingDefaultPassword"] = isUsingDefaultPassword();
    doc["firmwareVersion"] = CAR_TOUCH_FIRMWARE_VERSION;
    doc["wifiConnected"] = WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_AP;
    doc["wifiMode"] = (WiFi.getMode() == WIFI_AP) ? "AP" : ((WiFi.status() == WL_CONNECTED) ? "STA" : "OFFLINE");
    doc["ip"] = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    doc["bleEnabled"] = bleManager.isEnabled();
    doc["bleConnected"] = bleManager.isConnected();
    doc["bleOtaInProgress"] = bleManager.isOtaInProgress();
    AppConfig* cfg = getConfig();
    doc["canTxPin"] = cfg->canTxPin;
    doc["canRxPin"] = cfg->canRxPin;
    doc["canSpeed"] = cfg->canSpeed;
    doc["listenOnlyMode"] = cfg->listenOnlyMode;
    doc["can1CsPin"] = cfg->can1CsPin;
    doc["can1IntPin"] = cfg->can1IntPin;
    doc["can1Speed"] = cfg->can1Speed;
    doc["can1ListenOnly"] = cfg->can1ListenOnly;

    if (_moduleStatus) {
        JsonArray modules = doc["modules"].to<JsonArray>();
        for (uint8_t i = 0; i < MODULE_COUNT; ++i) {
            JsonObject item = modules.add<JsonObject>();
            item["id"] = i;
            item["name"] = _moduleStatus->name((ModuleId)i);
            item["state"] = _moduleStatus->stateText(_moduleStatus->getState((ModuleId)i));
            item["ready"] = (_moduleStatus->getState((ModuleId)i) == MODULE_READY);
        }
    }

    if (_profileManager) {
        char vehicleName[48];
        _profileManager->getActiveVehicleName(vehicleName, sizeof(vehicleName));
        doc["activeVehicle"] = vehicleName;
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ 404
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void WebServerManager::_handleNotFound(AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "404 - Not Found");
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Broadcasts
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

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

    JsonDocument doc;
    doc["type"] = "status";
    doc["message"] = status ? status : "";
    String msg;
    serializeJson(doc, msg);

    for (AsyncWebSocketClient& client : _ws.getClients()) {
        WsClientAuth* auth = _findClientAuth(client.id());
        if (auth && auth->authenticated) {
            client.text(msg);
        }
    }
}

void WebServerManager::broadcastCanDiagnostics(const CanDiagnostics& diagnostics,
                                                const char* interfaceName) {
    if (!_started) return;

    JsonDocument doc;
    doc["type"] = "can_diagnostics";
    doc["interface"] = interfaceName ? interfaceName : "CAN0";
    doc["driverReady"] = diagnostics.driverReady;
    doc["listenOnly"] = diagnostics.listenOnly;
    doc["busActive"] = diagnostics.busActive;
    doc["busOff"] = diagnostics.busOff;
    doc["rxFrames"] = diagnostics.rxFrames;
    doc["txFrames"] = diagnostics.txFrames;
    doc["errorCount"] = diagnostics.errorCount;
    doc["txFailedCount"] = diagnostics.txFailedCount;
    doc["rxMissedCount"] = diagnostics.rxMissedCount;
    doc["rxOverrunCount"] = diagnostics.rxOverrunCount;
    doc["arbitrationLostCount"] = diagnostics.arbitrationLostCount;
    doc["busErrorCount"] = diagnostics.busErrorCount;
    doc["txErrorCounter"] = diagnostics.txErrorCounter;
    doc["rxErrorCounter"] = diagnostics.rxErrorCounter;
    doc["msgsWaiting"] = diagnostics.msgsWaiting;

    String json;
    serializeJson(doc, json);
    for (AsyncWebSocketClient& client : _ws.getClients()) {
        WsClientAuth* auth = _findClientAuth(client.id());
        if (auth && auth->authenticated) client.text(json);
    }
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ VehicleData -> JSON
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void WebServerManager::broadcastModuleStatus() {
    if (!_started || !_moduleStatus) return;

    JsonDocument doc;
    doc["type"] = "module_status";
    JsonArray modules = doc["modules"].to<JsonArray>();
    for (uint8_t i = 0; i < MODULE_COUNT; ++i) {
        JsonObject item = modules.add<JsonObject>();
        item["id"] = i;
        item["name"] = _moduleStatus->name((ModuleId)i);
        item["state"] = _moduleStatus->stateText(_moduleStatus->getState((ModuleId)i));
        item["ready"] = (_moduleStatus->getState((ModuleId)i) == MODULE_READY);
    }
    String json;
    serializeJson(doc, json);
    for (AsyncWebSocketClient& client : _ws.getClients()) {
        WsClientAuth* auth = _findClientAuth(client.id());
        if (auth && auth->authenticated) client.text(json);
    }
}

String WebServerManager::_vehicleDataToJSON(const VehicleData& data) {
    JsonDocument doc;

    doc["type"]           = "vehicle_data";
    doc["speed"]              = data.vehicleSpeed;
    doc["rpm"]                    = data.engineRPM;
    doc["coolantTemp"]                = data.coolantTemp;
    if (ctBatteryVoltageAvailable(data.batteryVoltage)) {
        doc["battery"] = data.batteryVoltage;
    } else {
        doc["battery"] = nullptr;
    }
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

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Client status
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

bool WebServerManager::isClientConnected() {
    return _ws.count() > 0;
}

uint8_t WebServerManager::getClientCount() {
    return _ws.count();
}
