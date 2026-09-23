/**
 * main.cpp - CarTouch entry point
 *
 * Wires together the core modules (CAN, OBD-II, vehicle control, TFT UI,
 * web server, WiFi) plus the Learn Mode stack (CustomVehicleStore,
 * LearnEngine, ActiveProfileManager). VehicleControl resolves commands
 * through ActiveProfileManager rather than taking a raw CAN ID.
 */

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "can_manager.h"
#include "obd2_reader.h"
#include "vehicle_control.h"
#include "vehicle_db.h"
#include "tft_ui.h"
#include "webserver.h"
#include "wifi_manager.h"

#include "custom_vehicle_store.h"
#include "learn_engine.h"
#include "active_profile_manager.h"

// ============================================================================
// Global objects
// ============================================================================

CANManager canManager(PIN_CAN_TX, PIN_CAN_RX, CAN_SPEED);
OBD2Reader obd2Reader(canManager);

// Vehicle database (DBC). Allocated on the heap in setup() rather than as
// a static/global object: with MAX_DBC_MESSAGES=150, this object is
// roughly 460 KB, which would overflow the ESP32-S3's internal DRAM
// (~512 KB total, shared with WiFi/LVGL/stacks) if placed in .bss. With
// PSRAM enabled, the Arduino-ESP32 core automatically serves allocations
// larger than 4 KB from PSRAM - no special attribute needed.
// ActiveProfileManager and VehicleControl are allocated the same way
// since they hold references to the objects before them.
VehicleDB* vehicleDB = nullptr;

CustomVehicleStore customVehicleStore;
LearnEngine learnEngine(canManager);
ActiveProfileManager* activeProfileManager = nullptr;

VehicleControl* vehicleControl = nullptr;  // Resolves commands via activeProfileManager

TFT_UI tftUI;
WebServerManager webServer;
WiFiManager wifiManager;

// ============================================================================
// Global state
// ============================================================================

VehicleData currentVehicleData;
uint32_t    lastDataUpdateTime = 0;
uint32_t    lastActivityTime   = 0;
uint32_t    obdReadInterval    = 200;  // OBD poll interval, ms
DeviceMode  currentMode        = MODE_ACTIVE;

// Task watchdog timeout. If any stage of loop() stalls longer than this
// (e.g. a still-blocking OBD2Reader call, an unexpected infinite loop),
// the chip resets itself rather than hanging indefinitely in the vehicle.
#define WDT_TIMEOUT_S 8

// ============================================================================
// Forward declarations
// ============================================================================

void setup();
void loop();
void handleCommand(const char* command);
void handleControlCommand(const char* command);
void updateVehicleData();
void checkAutoSleep();
void wakeFromSleep();

// ============================================================================
// setup()
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n========================================");
    Serial.println(" CarTouch v2.0 - ESP32-S3 Car Control");
    Serial.println(" (+ Learn Mode / Custom Vehicle Database)");
    Serial.println("========================================\n");

    // Heap-allocate the large/interdependent objects before anything else
    // touches them.
    vehicleDB             = new VehicleDB();
    activeProfileManager  = new ActiveProfileManager(*vehicleDB, customVehicleStore);
    vehicleControl         = new VehicleControl(canManager, *activeProfileManager);
    if (!vehicleDB || !activeProfileManager || !vehicleControl) {
        Serial.println("[INIT] FATAL: allocation failed for vehicleDB/activeProfileManager/vehicleControl");
        Serial.println("[INIT] FATAL: PSRAM may be unavailable/disabled - halting.");
        while (true) { delay(1000); }
    }
    Serial.printf("[INIT] Free PSRAM: %u bytes | Free heap: %u bytes\n",
                  (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());

    // Watchdog - set up as early as possible so it covers the rest of
    // setup() too. Struct-based esp_task_wdt_config_t only exists on
    // Arduino-ESP32 3.x (ESP-IDF 5.x); this branch keeps the code
    // compiling on both 2.x and 3.x cores.
    {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        esp_task_wdt_config_t wdtConfig = {
            .timeout_ms     = WDT_TIMEOUT_S * 1000,
            .idle_core_mask = 0,
            .trigger_panic  = true
        };
        esp_task_wdt_init(&wdtConfig);
#else
        esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
        esp_task_wdt_add(NULL);
        Serial.printf("[INIT] Watchdog enabled (timeout: %ds)\n", WDT_TIMEOUT_S);
    }

    // 1. Configuration
    Serial.println("[INIT] Loading configuration...");
    loadConfig();

    // 2. SPIFFS (web assets, DBC files, custom profiles)
    Serial.println("[INIT] Starting SPIFFS...");
    if (!SPIFFS.begin(false)) {
        Serial.println("[INIT] SPIFFS error - formatting...");
        SPIFFS.format();
        if (!SPIFFS.begin(true)) {
            Serial.println("[INIT] SPIFFS still failing after format");
        }
    } else {
        Serial.println("[INIT] SPIFFS ready");
    }

    // 3. CAN Bus
    Serial.println("[INIT] Starting CAN Bus...");
    if (!canManager.begin()) {
        Serial.println("[INIT] CAN Bus failed to start - check wiring");
        tftUI.showNotification("CAN Bus error!");
    } else {
        canManager.flushRxQueue();
        tftUI.setCANStatus(true);
    }

    // 4. OBD-II reader
    obd2Reader.begin();

    // 5. Vehicle DB (built-in DBC files)
    vehicleDB->begin();

    // 6. Custom vehicle store (must come after SPIFFS.begin)
    Serial.println("[INIT] Starting CustomVehicleStore...");
    customVehicleStore.begin();

    // 7. Vehicle control
    vehicleControl->begin();

    // No vehicle is auto-selected at boot - ActiveProfileManager starts
    // with ACTIVE_KIND_NONE. The user picks one from the TFT or web UI;
    // until then, resolveCommand() reports "no vehicle selected" instead
    // of sending anything.

    // 8. TFT + LVGL. Learn Mode modules must be attached before begin() -
    // otherwise the Learn tab's internal pointers stay null.
    tftUI.attachLearnModules(&learnEngine, &customVehicleStore,
                              activeProfileManager, vehicleControl);
    tftUI.begin();
    tftUI.setControlCallback(handleCommand);
    tftUI.showNotification("CarTouch ready");

    // 9. WiFi (AP mode by default)
    wifiManager.begin(1);
    tftUI.setWiFiStatus(wifiManager.isConnected());

    // 10. Web server - attach Learn Mode modules before begin()
    webServer.attachLearnModules(&learnEngine, &customVehicleStore,
                                  activeProfileManager, vehicleControl);

    // 11. Start web server
    webServer.begin();
    webServer.setCommandCallback(handleCommand);

    lastActivityTime = millis();

    Serial.println("\n[INIT] CarTouch v2.0 ready");
    Serial.printf("[INIT] IP: %s\n", wifiManager.getIP().toString().c_str());
    Serial.printf("[INIT] CAN: %s\n", canManager.isActive() ? "OK" : "FAILED");
    Serial.printf("[INIT] Custom profiles found: %d\n", customVehicleStore.getProfileCount());

    if (isUsingDefaultPassword()) {
        Serial.println("[SECURITY] Web password is still the default! Change it from Settings.");
        tftUI.showNotification("Please change the default password!");
    }
}

// ============================================================================
// loop()
// ============================================================================

void loop() {
    // Feed the watchdog every iteration.
    esp_task_wdt_reset();

    // 1. LVGL
    tftUI.update();

    // 2. WebSocket
    webServer.update();

    // 3. Learn engine (non-blocking; must run every iteration for correct
    // baseline/action capture timing).
    learnEngine.update();

    // 4. OBD-II polling (fully non-blocking). No requests are sent while
    // Listen-Only is active, since reading OBD data requires transmitting
    // a request. obd2Reader.update() advances one small step per call;
    // getLatestData() picks up the result once a full round completes.
    if (currentMode == MODE_ACTIVE && !getConfig()->listenOnlyMode) {
        obd2Reader.update();

        if (millis() - lastDataUpdateTime > obdReadInterval) {
            if (obd2Reader.getLatestData(currentVehicleData)) {
                // Battery voltage over CAN isn't supported on most vehicles
                currentVehicleData.batteryVoltage = 12.6f;  // Placeholder
                tftUI.updateVehicleData(currentVehicleData);
                webServer.broadcastVehicleData(currentVehicleData);
            }
            lastDataUpdateTime = millis();
        }
    }

    // 5. Auto-sleep check. Deferred while a Learn Mode capture is in
    // progress so the session isn't interrupted.
    if (learnEngine.getState() == LEARN_IDLE) {
        checkAutoSleep();
    } else {
        lastActivityTime = millis();  // Active learning counts as activity
    }

    // 6. Wake on CAN activity while asleep
    if (currentMode == MODE_SLEEP || currentMode == MODE_DEEP_SLEEP) {
        CanMessage wakeMsg;
        if (canManager.receiveMessageNonBlocking(wakeMsg)) {
            wakeFromSleep();
        }
    }

    delay(5);  // Yield briefly
}

// ============================================================================
// Vehicle data (legacy blocking path - no longer called from loop())
// ============================================================================
// Superseded by the non-blocking obd2Reader.update()/getLatestData() flow
// in loop() step 4. Kept only for any code that may still call it directly.

void updateVehicleData() {
    obd2Reader.readAllPIDs(currentVehicleData);  // [BLOCKING]

    currentVehicleData.batteryVoltage = 12.6f;  // Placeholder

    tftUI.updateVehicleData(currentVehicleData);
    webServer.broadcastVehicleData(currentVehicleData);
}

// ============================================================================
// Command handling
// ============================================================================

void handleCommand(const char* command) {
    lastActivityTime = millis();

    Serial.printf("[CMD] Received: %s\n", command);

    if (getConfig()->listenOnlyMode) {
        if (strcmp(command, "listen_only") == 0 ||
            strcmp(command, "vehicle_select") == 0 ||
            strcmp(command, "toggle_theme") == 0) {
            handleControlCommand(command);
        } else {
            Serial.println("[CMD] Listen-Only mode - control command rejected");
            tftUI.showNotification("Listen-Only mode is active");
        }
        return;
    }

    handleControlCommand(command);
}

void handleControlCommand(const char* command) {
    bool result = false;

    if (strcmp(command, "lock") == 0) {
        result = vehicleControl->lockAllDoors();
    }
    else if (strcmp(command, "unlock") == 0) {
        result = vehicleControl->unlockAllDoors();
    }
    else if (strcmp(command, "windows_up") == 0) {
        result = vehicleControl->allWindowsUp();
    }
    else if (strcmp(command, "windows_down") == 0) {
        result = vehicleControl->allWindowsDown();
    }
    else if (strcmp(command, "sunroof") == 0) {
        result = vehicleControl->sunroofOpen();
    }
    else if (strcmp(command, "trunk") == 0) {
        result = vehicleControl->trunkOpen();
    }
    else if (strcmp(command, "mirror") == 0) {
        result = vehicleControl->foldMirrors();
    }
    else if (strcmp(command, "alarm") == 0) {
        if (currentVehicleData.alarmState == ALARM_DISARMED) {
            result = vehicleControl->alarmArm();
            currentVehicleData.alarmState = ALARM_ARMED;
        } else {
            result = vehicleControl->alarmDisarm();
            currentVehicleData.alarmState = ALARM_DISARMED;
        }
    }
    else if (strcmp(command, "listen_only") == 0) {
        AppConfig* cfg = getConfig();
        bool newMode = !cfg->listenOnlyMode;

        // reconfigureMode() performs a real driver uninstall/reinstall,
        // so the TWAI driver actually switches mode immediately - not
        // just the config flag (which previously only took effect after
        // a full reboot).
        if (!canManager.reconfigureMode(newMode)) {
            tftUI.showNotification("CAN mode switch failed - please restart the device");
            Serial.println("[CMD] reconfigureMode failed - driver state unknown");
            return;
        }

        cfg->listenOnlyMode = newMode;
        saveConfig();
        tftUI.showNotification(cfg->listenOnlyMode ?
            "Listen-Only mode enabled" : "Normal mode enabled");
        Serial.printf("[CMD] Listen-Only: %s (driver mode switched)\n",
                      cfg->listenOnlyMode ? "ON" : "OFF");
        return;
    }
    else if (strcmp(command, "toggle_theme") == 0) {
        AppConfig* cfg = getConfig();
        cfg->theme = (cfg->theme == THEME_DAY) ? THEME_NIGHT : THEME_DAY;
        saveConfig();
        tftUI.setTheme(cfg->theme);
        return;
    }
    else if (strcmp(command, "vehicle_select") == 0) {
        tftUI.showNotification("Select vehicle from the menu");
        return;
    }
    else if (strncmp(command, "vehicle_select_dbc:", 19) == 0) {
        // Format: "vehicle_select_dbc:Brand|Model"
        char buf[64];
        strncpy(buf, command + 19, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* sep = strchr(buf, '|');
        if (sep) {
            *sep = '\0';
            activeProfileManager->selectDBCVehicle(buf, sep + 1);
            tftUI.showNotification("Vehicle (DBC) selected");
        }
        return;
    }
    else if (strncmp(command, "vehicle_select_custom:", 22) == 0) {
        uint8_t idx = (uint8_t)atoi(command + 22);
        if (activeProfileManager->selectCustomVehicle(idx)) {
            tftUI.showNotification("Vehicle (custom) selected");
        } else {
            tftUI.showNotification("Profile not found");
        }
        return;
    }
    else {
        Serial.printf("[CMD] Unknown command: %s\n", command);
        tftUI.showNotification("Unknown command");
        return;
    }

    if (result) {
        tftUI.showNotification("Command sent");
        webServer.broadcastStatus(command);
    } else {
        String reason = vehicleControl->getLastErrorMessage();
        if (reason.length() > 0) {
            tftUI.showNotification(reason.c_str());
        } else {
            tftUI.showNotification("Command failed");
        }
        Serial.printf("[CMD] Command execution failed: %s\n", command);
    }
}

// ============================================================================
// Sleep / wake
// ============================================================================

void checkAutoSleep() {
    if (currentMode != MODE_ACTIVE) return;

    AppConfig* cfg = getConfig();
    uint32_t inactivityTime = millis() - lastActivityTime;

    if (inactivityTime >= cfg->sleepTimeout) {
        Serial.println("[SLEEP] Entering sleep mode (inactivity timeout)");
        currentMode = MODE_SLEEP;

        tftUI.setDeviceMode(MODE_SLEEP);
        wifiManager.disconnect();

        Serial.println("[SLEEP] Device asleep - waiting for CAN activity to wake");
    }
}

void wakeFromSleep() {
    if (currentMode == MODE_ACTIVE) return;

    Serial.println("[WAKE] Waking from sleep...");

    currentMode = MODE_ACTIVE;
    lastActivityTime = millis();

    tftUI.setDeviceMode(MODE_ACTIVE);

    if (!wifiManager.isConnected()) {
        wifiManager.begin(1);
    }

    tftUI.showNotification("Awake!");

    Serial.println("[WAKE] Device is awake");
}
