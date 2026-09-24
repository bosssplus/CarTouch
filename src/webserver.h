/**
 * webserver.h - Web server and WebSocket vehicle control
 *
 * WebSocket authentication:
 * Previously the WebSocket connection had no authentication at all -
 * anyone who knew the device's IP could connect directly to /ws and
 * send lock/unlock/trunk commands without ever going through the login
 * screen.
 *
 * Fix: after a successful HTTP login (/login), the browser receives a
 * one-time random session token. The client must send that same token
 * as its first WebSocket message (type "auth"). Until a client is
 * authenticated, no "command" message from it is accepted.
 *
 * This is a lightweight defense layer appropriate for an embedded
 * device, not full JWT/OAuth - but a significant improvement over no
 * authentication at all.
 *
 * Learn Mode (v2.0): see CarTouch_SPEC.md section 7.5. New WebSocket
 * message types (learn_start, learn_capture_baseline, ...) and new REST
 * endpoints for managing custom vehicle profiles were added - all going
 * through the same auth/rate-limit infrastructure above, with no
 * unauthenticated parallel path.
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

#define WS_MAX_CLIENTS          8        // Max WebSocket clients whose auth state is tracked at once
#define SESSION_TOKEN_TIMEOUT   900000   // Session token validity, ms (15 minutes)
#define COMMAND_RATE_LIMIT_MS   300      // Minimum spacing between control commands per client, ms

typedef void (*WebCommandCallback)(const char* command);

// Per-WebSocket-client authentication state
struct WsClientAuth {
    uint32_t clientId          = 0;
    bool     authenticated       = false;
    uint32_t lastCommandTime       = 0;
    bool     inUse                   = false;
};

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
     * Attaches the Learn Mode / custom-profile modules. Must be called
     * once in setup(), before begin() (same pattern as
     * setCommandCallback). Kept as a separate method so the
     * WebServerManager constructor stays untouched and setup() ordering
     * in main.cpp remains flexible.
     */
    void attachLearnModules(LearnEngine* learnEngine,
                            CustomVehicleStore* customStore,
                            ActiveProfileManager* profileManager,
                            VehicleControl* vehicleControl);

    /**
     * Invalidates every active web session - both the HTTP session
     * token and every connected WebSocket client's auth state.
     * Registered as a PasswordChangeCallback (see config.h) via
     * registerPasswordChangeCallback so it fires whenever the password
     * is changed from *either* interface (TFT or web), not only when
     * changed from the web itself.
     */
    void invalidateAllSessions();

private:
    AsyncWebServer      _server;
    AsyncWebSocket       _ws;
    WebCommandCallback     _commandCallback;
    bool                      _started;

    // Current session token (issued after a successful login) and when it was issued
    String    _sessionToken;
    uint32_t  _sessionTokenIssuedAt;

    // Auth state for each connected WebSocket client
    WsClientAuth _clientAuth[WS_MAX_CLIENTS];

    // Learn Mode module pointers - raw pointers (not references) since
    // they may still be null before attachLearnModules() runs; every
    // method that uses them must null-check.
    LearnEngine*             _learnEngine;
    CustomVehicleStore*        _customStore;
    ActiveProfileManager*        _profileManager;
    VehicleControl*                _vehicleControl;

    void _handleWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                               AwsEventType type, void* arg, uint8_t* data, size_t len);
    void _handleAPIControl(AsyncWebServerRequest* request);
    void _handleAPIStatus(AsyncWebServerRequest* request);
    void _handleNotFound(AsyncWebServerRequest* request);

    // -- OTA update via web (/update) - behind the same authentication ------------
    void _handleOtaUpload(AsyncWebServerRequest* request, const String& filename,
                          size_t index, uint8_t* data, size_t len, bool final);
    void _handleOtaFinished(AsyncWebServerRequest* request);
    String    _otaError;          // Most recent upload error (empty = none)
    size_t     _otaBytes;          // Bytes written so far in the current upload
    bool        _otaIsFs;           // true = filesystem image upload (spiffs.bin)
    bool         _rebootPending;      // Reboot after a successful OTA (handled in update())
    uint32_t      _rebootAt;

    // Static instance pointer - registerPasswordChangeCallback only
    // accepts a plain, non-capturing function pointer, so this mirrors
    // the pThisUI pattern in tft_ui.cpp.
    static WebServerManager* _instance;
    static void _staticInvalidateSessions();  // Non-capturing bridge to invalidateAllSessions()

    bool    _authenticate(AsyncWebServerRequest* request);
    String   _generateSessionToken();
    bool      _isValidSessionToken(const char* token);

    WsClientAuth* _findOrCreateClientAuth(uint32_t clientId);
    WsClientAuth* _findClientAuth(uint32_t clientId);
    void           _removeClientAuth(uint32_t clientId);

    String _vehicleDataToJSON(const VehicleData& data);

    // Learn Mode WebSocket message handling - only called after a client
    // is fully authenticated (auth->authenticated==true), same as
    // existing "command" messages.
    void _handleLearnModeMessage(AsyncWebSocketClient* client, JsonDocument& doc, const char* type);

    String _learnStateToJSON();  // Serializes the learn engine's current state for the client

    void _registerCustomVehicleRoutes();  // Custom-profile management REST endpoints
};

#endif // WEBSERVER_H
