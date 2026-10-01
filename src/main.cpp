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
#include "ct_can_config.h"
#include "can_manager.h"
#include "can_service.h"
#include "mcp2515_can_interface.h"
#include "obd2_reader.h"
#include "vehicle_control.h"
#include "vehicle_db.h"
#include "tft_ui.h"
#include "webserver.h"
#include "wifi_manager.h"
#include "ble_manager.h"
#include "module_status.h"

#include "custom_vehicle_store.h"
#include "learn_engine.h"
#include "active_profile_manager.h"
#include "error_log.h"

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Global objects
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

CANManager can0Interface(PIN_CAN_TX, PIN_CAN_RX, CAN_SPEED);
Mcp2515CanInterface can1Interface;
CANService canManager(can0Interface, can1Interface);
OBD2Reader obd2Reader(canManager);

// Vehicle database (DBC). Allocated on the heap in setup() rather than as
// a static/global object because the parser owns dynamic signal vectors and
// the message table can represent large real-world DBC files. With PSRAM
// enabled, the Arduino-ESP32 allocator can place these larger allocations
// outside the limited internal DRAM pool.
// ActiveProfileManager and VehicleControl are allocated the same way
// since they hold references to the objects before them.
VehicleDB* vehicleDB = nullptr;

CustomVehicleStore customVehicleStore;
LearnEngine learnEngine(canManager);
ActiveProfileManager* activeProfileManager = nullptr;

VehicleControl* vehicleControl = nullptr;    // Resolves commands via activeProfileManager

TFT_UI tftUI;
WebServerManager webServer;
WiFiManager wifiManager;
ModuleStatusManager moduleStatusManager;
bool filesystemReady = false;
bool customVehicleStoreReady = false;

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Global state
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

VehicleData currentVehicleData;
uint32_t lastDataUpdateTime = 0;
uint32_t lastActivityTime   = 0;
uint32_t lastWakeTime       = 0;
static constexpr uint32_t WAKE_OBD_TX_HOLD_MS = 1000;
uint32_t    obdReadInterval    = 200;            // OBD poll interval, ms
DeviceMode  currentMode        = MODE_ACTIVE;

// Task watchdog timeout. If any stage of loop() stalls longer than this
// (e.g. a still-blocking OBD2Reader call, an unexpected infinite loop),
// the chip resets itself rather than hanging indefinitely in the vehicle.
#define WDT_TIMEOUT_S 8

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Forward declarations
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

void setup();
void loop();
void handleCommand(const char* command);      // thread-safe: enqueue only
void processCommand(const char* command);     // runs in loop() task
void drainCommandQueue();
void handleControlCommand(const char* command);
void checkAutoSleep();
void wakeFromSleep();
void refreshModuleStatuses();

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ setup()
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n========================================");
    Serial.println(" CarTouch - ESP32-S3 Car Control");
    Serial.println(" (+ Learn Mode / Custom Vehicle Database)");
    Serial.println("========================================\n");

    const uint32_t flashBytes = ESP.getFlashChipSize();
    const bool psramAvailable = ESP.getPsramSize() > 0;
    const bool partitionLayoutFits = ctPartitionFitsFlash(flashBytes, CT_REQUIRED_FLASH_BYTES);
    Serial.printf("[INIT] Flash: %u bytes | PSRAM: %u bytes | heap: %u bytes\n",
                  (unsigned)flashBytes, (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreeHeap());
    if (!partitionLayoutFits) {
        Serial.printf("[INIT] Configured partition layout needs %u bytes of flash; filesystem will remain disabled.\n",
                      (unsigned)CT_REQUIRED_FLASH_BYTES);
    }
    if (!psramAvailable) {
        Serial.println("[INIT] PSRAM unavailable; runtime memory is limited to internal SRAM.");
    }

    // Keep the large core objects resident in static storage so they remain
    // usable even on ESP32-S3 boards without PSRAM. A small board should not
    // hard-fail just because a 16MB+PSRAM layout is not available.
    static VehicleDB vehicleDBStorage;
    static ActiveProfileManager activeProfileManagerStorage(vehicleDBStorage, customVehicleStore);
    static VehicleControl vehicleControlStorage(canManager, activeProfileManagerStorage);
    vehicleDB = &vehicleDBStorage;
    activeProfileManager = &activeProfileManagerStorage;
    vehicleControl = &vehicleControlStorage;
    Serial.printf("[INIT] Configured partition layout fits detected flash: %s\n",
                  partitionLayoutFits ? "yes" : "no");

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
        esp_err_t wdtErr = esp_task_wdt_init(&wdtConfig);
#else
        esp_err_t wdtErr = esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
        // The core may already have initialised the TWDT (with its own
        // timeout); in that case the call fails and the core's timeout stays.
        if (wdtErr != ESP_OK) {
            Serial.printf("[INIT] Watchdog init returned %d - core default timeout stays\n", (int)wdtErr);
        }
        // NOTE: the loop task is subscribed at the END of setup(), not here.
        // setup() contains long blocking steps (DBC loading,
        // touch calibration) that would otherwise trip the watchdog and cause
        // a reboot loop.
    }

    // 1. Configuration
    Serial.println("[INIT] Loading configuration...");
    loadConfig();

    // 1b. Error log / telemetry (checklist item 16) - started right
    // after config so every subsequent init step can log through it.
    getErrorLog()->begin();
    moduleStatusManager.begin();

    // 2. SPIFFS (web assets, DBC files, custom profiles)
    Serial.println("[INIT] Starting SPIFFS...");
    if (!partitionLayoutFits) {
        Serial.println("[INIT] SPIFFS disabled because its configured partition exceeds detected flash; data was not formatted");
        getErrorLog()->log(LOG_CAT_SYSTEM, LOG_ERROR,
                           "Configured SPIFFS partition does not fit detected flash; filesystem disabled without formatting");
        moduleStatusManager.setState(MODULE_STORAGE, MODULE_ERROR);
    } else if (!SPIFFS.begin(false)) {
        Serial.println("[INIT] SPIFFS mount failed - preserving data; filesystem features disabled");
        getErrorLog()->log(LOG_CAT_SYSTEM, LOG_ERROR,
                           "SPIFFS mount failed; filesystem features disabled without formatting");
        moduleStatusManager.setState(MODULE_STORAGE, MODULE_ERROR);
    } else {
        filesystemReady = true;
        Serial.println("[INIT] SPIFFS ready");
        moduleStatusManager.setState(MODULE_STORAGE, MODULE_READY);
    }

    // 3. CAN Bus
    Serial.println("[INIT] Starting CAN Bus...");
    moduleStatusManager.setState(MODULE_CAN, MODULE_INITIALIZING);
    moduleStatusManager.setState(MODULE_CAN1, MODULE_INITIALIZING);
    if (!canManager.begin()) {
        getErrorLog()->log(LOG_CAT_CAN, LOG_ERROR, "CAN Bus failed to start at boot");
        moduleStatusManager.setState(MODULE_CAN, MODULE_ERROR);
    } else {
        canManager.flushRxQueue();
        moduleStatusManager.setState(MODULE_CAN, MODULE_READY);
    }
    if (canManager.isActive(CAN_BUS_1)) {
        const AppConfig* config = getConfig();
        Serial.printf("[CAN1] MCP2515 ready (8 MHz oscillator, %lu bps, %s)\n",
                      (unsigned long)config->can1Speed,
                      config->can1ListenOnly ? "listen-only" : "normal");
        moduleStatusManager.setState(MODULE_CAN1, MODULE_READY);
    } else {
        Serial.println("[CAN1] MCP2515 unavailable; CAN0 remains independent");
        getErrorLog()->log(LOG_CAT_CAN, LOG_WARN, "CAN1 MCP2515 failed to initialize");
        moduleStatusManager.setState(MODULE_CAN1, MODULE_ERROR);
    }

    // 4. OBD-II reader
    moduleStatusManager.setState(MODULE_OBD, MODULE_INITIALIZING);
    obd2Reader.begin();
    moduleStatusManager.setState(MODULE_OBD,
        !canManager.isActive() ? MODULE_ERROR :
        (getConfig()->listenOnlyMode ? MODULE_DISABLED : MODULE_READY));

    // 5. Vehicle DB (built-in DBC files)
    vehicleDB->begin();

    // 6. Custom vehicle store (must come after SPIFFS.begin)
    Serial.println("[INIT] Starting CustomVehicleStore...");
    if (filesystemReady) {
        customVehicleStoreReady = customVehicleStore.begin();
        if (!customVehicleStoreReady) {
            getErrorLog()->log(LOG_CAT_SYSTEM, LOG_ERROR, "Custom profile storage failed to initialize");
            moduleStatusManager.setState(MODULE_STORAGE, MODULE_ERROR);
        }
    }

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
#ifdef CARTOUCH_HEADLESS
    moduleStatusManager.setState(MODULE_DISPLAY, MODULE_DISABLED);
    moduleStatusManager.setState(MODULE_TOUCH, MODULE_DISABLED);
    Serial.println("[INIT] Headless build: display and touch are disabled");
#else
    moduleStatusManager.setState(MODULE_DISPLAY, MODULE_INITIALIZING);
    tftUI.begin();
    moduleStatusManager.setState(MODULE_DISPLAY, MODULE_READY);
    tftUI.setControlCallback(handleCommand);
    // Update the visual CAN state only after the TFT/LVGL objects exist.
    // The driver being initialized does not prove that a transceiver is
    // physically wired to a live bus; the status indicator is therefore
    // deliberately based on the controller's current state here and is
    // refined by runtime diagnostics in the UI.
    tftUI.setCANStatus(canManager.isActive());
    moduleStatusManager.setState(MODULE_TOUCH,
        tftUI.isTouchAvailable() ? MODULE_READY : MODULE_NOT_PRESENT);
    if (!canManager.isActive()) {
        tftUI.showNotification("CAN Bus error!");
    } else {
        tftUI.showNotification("CarTouch ready");
    }
#endif

    // 9. WiFi (AP mode by default)
    moduleStatusManager.setState(MODULE_WIFI, MODULE_INITIALIZING);
    wifiManager.begin(1);
    tftUI.setWiFiStatus(wifiManager.isConnected());
    moduleStatusManager.setState(MODULE_WIFI,
        wifiManager.isEnabled() && wifiManager.isConnected() ? MODULE_READY :
        (wifiManager.isEnabled() ? MODULE_ERROR : MODULE_DISABLED));

    // 10. BLE - starts independently of Wi-Fi so local BLE access remains available.
    moduleStatusManager.setState(MODULE_BLE, MODULE_INITIALIZING);
    if (!bleManager.begin()) {
        Serial.println("[BLE] Failed to start BLE");
        moduleStatusManager.setState(MODULE_BLE, MODULE_ERROR);
    } else {
        moduleStatusManager.setState(MODULE_BLE, MODULE_READY);
    }

    // 11. Web server - attach Learn Mode modules before begin()
    webServer.attachLearnModules(&learnEngine, &customVehicleStore,
                                  activeProfileManager, vehicleControl);
    webServer.setModuleStatusManager(&moduleStatusManager);

    // 12. Start web server
    moduleStatusManager.setState(MODULE_WEB, MODULE_INITIALIZING);
    webServer.begin();
    moduleStatusManager.setState(MODULE_WEB, webServer.isStarted() ? MODULE_READY : MODULE_ERROR);
    webServer.broadcastModuleStatus();
    webServer.setCommandCallback(handleCommand);

    lastActivityTime = millis();

    // Setup is complete: from here on loop() feeds the watchdog.
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();
    Serial.printf("[INIT] Watchdog enabled (timeout: %ds)\n", WDT_TIMEOUT_S);

    Serial.println("\n[INIT] CarTouch ready");
    Serial.printf("[INIT] IP: %s\n", wifiManager.getIP().toString().c_str());
    Serial.printf("[INIT] CAN: %s\n", canManager.isActive() ? "OK" : "FAILED");
    Serial.printf("[INIT] Custom profiles found: %d\n", customVehicleStore.getProfileCount());

    if (isUsingDefaultPassword()) {
        Serial.println("[SECURITY] Web password is still the default! Change it from Settings.");
        tftUI.showNotification("Please change the default password!");
    }
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ loop()
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

void loop() {
    // Feed the watchdog every iteration.
    esp_task_wdt_reset();

    // Flush error-log counters to NVS at most every 5 minutes (see
    // error_log.h) - cheap to call every loop() since it no-ops unless
    // both the dirty flag and the interval have elapsed.
    getErrorLog()->maybeSaveCounters();

    // 1. LVGL
    tftUI.update();

    // 2. WebSocket
    webServer.update();

    static uint32_t lastCanDiagnosticsBroadcast = 0;
    if ((uint32_t)(millis() - lastCanDiagnosticsBroadcast) >= 1000) {
        CanDiagnostics diagnostics = {};
        canManager.getDiagnostics(CAN_BUS_0, diagnostics);
        webServer.broadcastCanDiagnostics(diagnostics, "CAN0");

        static bool can1BusOffSeen = false;
        static uint32_t lastCan1Recovery = 0;
        diagnostics = {};
        canManager.getDiagnostics(CAN_BUS_1, diagnostics);
        if (diagnostics.busOff) {
            const uint32_t now = millis();
            if (!can1BusOffSeen || (uint32_t)(now - lastCan1Recovery) >= 5000) {
                getErrorLog()->log(LOG_CAT_CAN, LOG_ERROR, "CAN1 MCP2515 bus-off detected; attempting recovery");
                if (!canManager.recoverFromBusOff(CAN_BUS_1)) {
                    getErrorLog()->log(LOG_CAT_CAN, LOG_WARN, "CAN1 MCP2515 recovery failed; retrying in 5 seconds");
                }
                can1BusOffSeen = true;
                lastCan1Recovery = now;
                diagnostics = {};
                canManager.getDiagnostics(CAN_BUS_1, diagnostics);
            }
        } else {
            can1BusOffSeen = false;
        }
        webServer.broadcastCanDiagnostics(diagnostics, "CAN1");
        lastCanDiagnosticsBroadcast = millis();
    }

    // 2b. BLE / BLE OTA
    bleManager.update();

    // 2b'. Keep the module status (Wi-Fi/Web/CAN/OBD/Touch) live on TFT and Web.
    refreshModuleStatuses();

    // 2c. Run queued commands here so all CAN/UI/state access stays in this task
    drainCommandQueue();

    // 3. Learn engine (non-blocking; must run every iteration for correct
    // baseline/action capture timing).
    learnEngine.update();

    // 4. OBD-II polling (fully non-blocking). No requests are sent while
    // Listen-Only is active, since reading OBD data requires transmitting
    // a request. obd2Reader.update() advances one small step per call;
    // getLatestData() picks up the result once a full round completes.
    if (currentMode == MODE_ACTIVE &&
        !getConfig()->listenOnlyMode &&
        (millis() - lastWakeTime >= WAKE_OBD_TX_HOLD_MS)) {
        obd2Reader.update();

        if (millis() - lastDataUpdateTime > obdReadInterval) {
            if (obd2Reader.getLatestData(currentVehicleData)) {
                // Battery/control-module voltage comes from OBD-II PID 0x42
                // when the ECU supports it. Zero means unavailable; never
                // display a fabricated voltage value.
                tftUI.updateVehicleData(currentVehicleData);
                webServer.broadcastVehicleData(currentVehicleData);
            }
            lastDataUpdateTime = millis();
        }
    }

#ifndef CARTOUCH_HEADLESS
    // 4b. Touch activity: without this the device went to sleep (screen off,
    // Wi-Fi off) 10 minutes after the last *command* even while the user was
    // actively using the TFT. lv_disp_get_inactive_time() is LVGL's time
    // since the last pointer/touch event.
    if (lv_disp_get_inactive_time(NULL) < 1000) {
        if (currentMode == MODE_SLEEP) {
            wakeFromSleep();
        }
        lastActivityTime = millis();
    }
#endif

    // 5. Auto-sleep check. Deferred while a Learn Mode capture is in
    // progress so the session isn't interrupted.
    if (learnEngine.isActiveCaptureState()) {
        // Only real capture windows count as continuous activity. Terminal,
        // error and candidate-review states must not disable auto-sleep forever.
        lastActivityTime = millis();
    } else {
        checkAutoSleep();
    }

    // 6. Wake on CAN activity while asleep
    if (currentMode == MODE_SLEEP || currentMode == MODE_DEEP_SLEEP) {
        CanMessage wakeMsg;
        if (canManager.receiveMessageNonBlocking(wakeMsg)) {
            wakeFromSleep();
        }
    }

    delay(5);    // Yield briefly
}

// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○
// ○○○○○○○○○○ Command handling
// ○○○○○○○○○○○○○○○○○○○○○○○○○○○○○○

// handleCommand() is called from the async web task, the BLE task and the
// TFT callback. It only enqueues; processCommand() runs in loop() so that
// VehicleControl, currentVehicleData and LVGL are never touched concurrently.
static constexpr uint8_t CMD_QUEUE_SIZE = 8;
static constexpr size_t  CMD_MAX_LEN    = 64;
static char       cmdQueue[CMD_QUEUE_SIZE][CMD_MAX_LEN];
static uint8_t    cmdHead = 0, cmdTail = 0;
static portMUX_TYPE cmdMux = portMUX_INITIALIZER_UNLOCKED;

void handleCommand(const char* command) {
    if (!command || strlen(command) >= CMD_MAX_LEN) {
        Serial.println("[CMD] Rejected: empty or too long");
        return;
    }
    bool queued = false;
    portENTER_CRITICAL(&cmdMux);
    uint8_t next = (cmdHead + 1) % CMD_QUEUE_SIZE;
    if (next != cmdTail) {
        strcpy(cmdQueue[cmdHead], command);
        cmdHead = next;
        queued = true;
    }
    portEXIT_CRITICAL(&cmdMux);
    if (!queued) Serial.println("[CMD] Queue full - command dropped");
}

void drainCommandQueue() {
    char cmd[CMD_MAX_LEN];
    for (;;) {
        bool have = false;
        portENTER_CRITICAL(&cmdMux);
        if (cmdTail != cmdHead) {
            strcpy(cmd, cmdQueue[cmdTail]);
            cmdTail = (cmdTail + 1) % CMD_QUEUE_SIZE;
            have = true;
        }
        portEXIT_CRITICAL(&cmdMux);
        if (!have) break;
        processCommand(cmd);
    }
}

void processCommand(const char* command) {
    lastActivityTime = millis();

    Serial.printf("[CMD] Received: %s\n", command);

    // While asleep, reject physical-control commands instead of allowing a
    // Web/TFT event queued before/around wake to trigger immediate CAN TX.
    // Wake is driven by CAN activity; after wake the user can explicitly
    // issue a fresh command. Safe configuration/navigation commands remain
    // available.
    if (currentMode != MODE_ACTIVE &&
        strcmp(command, "listen_only") != 0 &&
        strcmp(command, "vehicle_select") != 0 &&
        strncmp(command, "vehicle_select_dbc:", 19) != 0 &&
        strncmp(command, "vehicle_select_custom:", 22) != 0 &&
        strcmp(command, "toggle_theme") != 0) {
        Serial.println("[CMD] Device asleep - control command rejected");
        tftUI.showNotification("Device is asleep - wake it before controlling");
        return;
    }

    if (getConfig()->listenOnlyMode) {
        if (strcmp(command, "listen_only") == 0 ||
            strcmp(command, "vehicle_select") == 0 ||
            strncmp(command, "vehicle_select_dbc:", 19) == 0 ||
            strncmp(command, "vehicle_select_custom:", 22) == 0 ||
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
            if (result) {
                currentVehicleData.alarmState = ALARM_ARMED;
            }
        } else {
            result = vehicleControl->alarmDisarm();
            if (result) {
                currentVehicleData.alarmState = ALARM_DISARMED;
            }
        }
    }
    else if (strcmp(command, "listen_only") == 0) {
        AppConfig* cfg     = getConfig();
        bool       newMode = !cfg->listenOnlyMode;

        // reconfigureMode() performs a real driver uninstall/reinstall,
        // so the TWAI driver switches mode immediately.
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
            if (activeProfileManager->selectDBCVehicle(buf, sep + 1)) {
                tftUI.showNotification("Vehicle (DBC) selected");
            } else {
                tftUI.showNotification("Vehicle profile not found");
            }
        } else {
            tftUI.showNotification("Invalid vehicle selection");
        }
        return;
    }
    else if (strncmp(command, "vehicle_select_custom:", 22) == 0) {
        char* endp = nullptr;
        long parsed = strtol(command + 22, &endp, 10);
        if (endp == command + 22 || *endp != '\0' || parsed < 0 || parsed > 255) {
            tftUI.showNotification("Invalid profile index");
            return;
        }
        uint8_t idx = (uint8_t)parsed;
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

void refreshModuleStatuses() {
    static uint32_t lastCheck = 0;
    static ModuleState lastStates[MODULE_COUNT] = {};
    const uint32_t now = millis();
    if (now - lastCheck < 500) return;
    lastCheck = now;

    const ModuleState states[MODULE_COUNT] = {
        (wifiManager.isEnabled() && wifiManager.isConnected()) ? MODULE_READY : (wifiManager.isEnabled() ? MODULE_ERROR : MODULE_DISABLED),
        webServer.isStarted() ? MODULE_READY : MODULE_ERROR,
        canManager.isActive() ? MODULE_READY : MODULE_ERROR,
        !canManager.isActive() ? MODULE_ERROR :
            (getConfig()->listenOnlyMode ? MODULE_DISABLED : MODULE_READY),
        tftUI.isTouchAvailable() ? MODULE_READY : MODULE_NOT_PRESENT,
        MODULE_READY,
        bleManager.isEnabled() ? MODULE_READY : MODULE_ERROR,
        (filesystemReady && customVehicleStoreReady) ? MODULE_READY : MODULE_ERROR,
        canManager.isActive(CAN_BUS_1) ? MODULE_READY : MODULE_ERROR
    };
    bool changed = false;
    for (uint8_t i = 0; i < MODULE_COUNT; ++i) {
        if (states[i] != lastStates[i]) {
            moduleStatusManager.setState((ModuleId)i, states[i]);
            lastStates[i] = states[i];
            changed = true;
        }
    }
    if (changed) {
        tftUI.setCANStatus(states[MODULE_CAN] == MODULE_READY);
        tftUI.setWiFiStatus(states[MODULE_WIFI] == MODULE_READY);
        for (uint8_t i = 0; i < MODULE_COUNT; ++i) {
            tftUI.setModuleStatus((ModuleId)i, states[i]);
        }
        webServer.broadcastModuleStatus();
    }
}

// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■
// □□□□□□□□□□ Sleep / wake
// ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■

void checkAutoSleep() {
    if (currentMode != MODE_ACTIVE) return;

    AppConfig* cfg            = getConfig();
    uint32_t   inactivityTime = millis() - lastActivityTime;

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
    // Do not let the first active loop immediately start OBD polling after a
    // CAN wake event. The wake frame itself may be unrelated to OBD, so hold
    // all OBD requests briefly and let the user/device settle first.
    lastWakeTime = lastActivityTime;

    tftUI.setDeviceMode(MODE_ACTIVE);

    if (!wifiManager.isConnected()) {
        wifiManager.begin(1);
    }

    tftUI.showNotification("Awake!");

    Serial.println("[WAKE] Device is awake");
}
