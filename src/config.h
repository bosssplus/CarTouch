/**
 * config.h - Global project configuration
 *
 * All constants, pin assignments, and user-adjustable settings live
 * here. Defaults are set below; users can change them from the TFT or
 * web settings menu, persisted via loadConfig()/saveConfig().
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// Hardware pins
// ============================================================================

// CAN Bus - SN65HVD230 transceiver
#define PIN_CAN_TX           9   // GPIO9  - CAN Transmit
#define PIN_CAN_RX           6   // GPIO6  - CAN Receive (moved from GPIO10 to avoid conflict with TFT_CS)

// TFT display - ILI9341, 2.8" SPI, 240x320, with XPT2046 touch
#define PIN_TFT_CS           10  // GPIO10 - Chip Select
#define PIN_TFT_DC             7  // GPIO7  - Data/Command
#define PIN_TFT_RST            4  // GPIO4  - Reset
#define PIN_TFT_MOSI          11  // GPIO11 - Master Out Slave In
#define PIN_TFT_SCLK          12  // GPIO12 - Serial Clock
#define PIN_TFT_MISO          13  // GPIO13 - Master In Slave Out
#define PIN_TFT_BL            21  // GPIO21 - Backlight

#define PIN_TOUCH_CS          14  // GPIO14 - Touch Chip Select

#define PIN_LED_INTERNAL      38  // GPIO38 - ESP32-S3 DevKit onboard LED

// ============================================================================
// CAN Bus settings
// ============================================================================

#define CAN_SPEED            500000  // 500 Kbps (standard OBD-II)
#define CAN_LISTEN_TIMEOUT   50       // Receive timeout, ms
#define CAN_MAX_RETRY        3         // Max retransmit attempts

// ============================================================================
// OBD-II settings
// ============================================================================

// Standard PIDs (SAE J1979)
#define OBD_PID_ENGINE_RPM     0x0C   // Engine RPM
#define OBD_PID_VEHICLE_SPEED  0x0D   // Vehicle speed (km/h)
#define OBD_PID_COOLANT_TEMP   0x05   // Coolant temperature (degC)
#define OBD_PID_BATTERY_VOLT   0x42   // Battery voltage (V) - non-standard, via a specific CAN ID
#define OBD_PID_THROTTLE_POS   0x11   // Throttle position (%)
#define OBD_PID_FUEL_LEVEL     0x2F   // Fuel level (%)
#define OBD_PID_RUNTIME        0x1F   // Engine runtime since start

// ============================================================================
// Display settings
// ============================================================================

#define TFT_WIDTH             240
#define TFT_HEIGHT            320
#define TFT_ROTATION            1     // 0-3
#define TFT_BRIGHTNESS_MAX     255
#define TFT_BRIGHTNESS_NIGHT    50
#define TFT_BRIGHTNESS_DAY     200

// ============================================================================
// LVGL settings
// ============================================================================

#define LVGL_TICK_MS            5
#define LVGL_BUF_SIZE           (TFT_WIDTH * 20)

// ============================================================================
// WiFi / web server settings
// ============================================================================

#define WIFI_AP_NAME          "CarTouch"
#define WIFI_AP_PASSWORD      "12345678"  // Temporary AP password - change from Settings ASAP (min 8 chars)
#define WIFI_MAX_RETRY        20
#define WIFI_TIMEOUT_MS       15000

#define WEB_PORT              80
#define WS_PORT               81

// These default credentials are visible to anyone who reads this public
// repository, so they must be treated as temporary, not final:
//  1. They are a first-boot placeholder only.
//  2. AppConfig::forcePasswordChange starts true and stays true until the
//     user changes the password from the TFT or web settings screen; the
//     device shows a persistent warning in the meantime (access itself
//     is not blocked, since the user may need the default credentials to
//     reach Settings in the first place).
#define WEB_DEFAULT_USER      "admin"
#define WEB_DEFAULT_PASS      "cartouch"

// ============================================================================
// Power management
// ============================================================================

#define AUTO_SLEEP_TIMEOUT           600000  // 10 minutes of inactivity, ms
#define CAN_WAKEUP_ID                 0x000   // CAN ID that wakes the device (0x000 = any)
#define DEEP_SLEEP_WAKEUP_DURATION       60   // Periodic wake interval in deep sleep, s

// ============================================================================
// Vehicle settings
// ============================================================================

#define MAX_DTC_COUNT                 20
#define CAN_BUS_VOLTAGE_DIVIDER      2.0f

// ============================================================================
// Learn Mode settings (v2.0) - see CarTouch_SPEC.md sections 4.3 and 7.7
// ============================================================================

#define MAX_CUSTOM_VEHICLES                8   // Max custom (Learned/Manual) profiles
#define MAX_LEARNED_COMMANDS_PER_VEHICLE   32   // Max commands per custom profile
#define LEARN_BASELINE_MS                2000   // Default baseline capture duration, ms
#define LEARN_ACTION_CAPTURE_MS          2000   // Default action capture duration, ms
#define LEARN_ACTION_CAPTURE_MAX_MS      5000   // Max allowed action capture duration, ms
#define BASELINE_MAX_IDS                   64   // Max distinct CAN IDs tracked during baseline
#define CANDIDATE_MAX                      10   // Max candidates shown after diffing

// ============================================================================
// Global data types
// ============================================================================

enum WindowState : uint8_t {
    WINDOW_UNKNOWN = 0,
    WINDOW_CLOSED  = 1,
    WINDOW_OPENING = 2,
    WINDOW_CLOSING = 3,
    WINDOW_OPEN    = 4
};

enum DoorLockState : uint8_t {
    LOCK_UNKNOWN  = 0,
    LOCK_LOCKED   = 1,
    LOCK_UNLOCKED = 2
};

enum AlarmState : uint8_t {
    ALARM_DISARMED  = 0,
    ALARM_ARMED     = 1,
    ALARM_TRIGGERED = 2
};

enum ThemeMode : uint8_t {
    THEME_DAY   = 0,
    THEME_NIGHT = 1,
    THEME_AUTO  = 2
};

enum DeviceMode : uint8_t {
    MODE_LISTEN_ONLY = 0,   // Listen-only - no commands are sent
    MODE_ACTIVE      = 1,   // Active - user can issue commands
    MODE_SLEEP       = 2,   // Low-power sleep
    MODE_DEEP_SLEEP  = 3    // Deep sleep - lowest power
};

// ============================================================================
// Vehicle data
// ============================================================================

struct VehicleData {
    // Powertrain
    uint16_t engineRPM      = 0;
    uint8_t  vehicleSpeed   = 0;      // km/h
    int8_t   coolantTemp    = -40;    // degC
    float    batteryVoltage = 0.0f;   // V
    uint8_t  throttlePos    = 0;      // %
    uint8_t  fuelLevel      = 0;      // %
    uint16_t engineRuntime  = 0;      // seconds

    // Doors
    DoorLockState doorFL      = LOCK_UNKNOWN;
    DoorLockState doorFR      = LOCK_UNKNOWN;
    DoorLockState doorRL      = LOCK_UNKNOWN;
    DoorLockState doorRR      = LOCK_UNKNOWN;
    DoorLockState trunkState  = LOCK_UNKNOWN;

    // Windows
    WindowState windowFL      = WINDOW_UNKNOWN;
    WindowState windowFR      = WINDOW_UNKNOWN;
    WindowState windowRL      = WINDOW_UNKNOWN;
    WindowState windowRR      = WINDOW_UNKNOWN;
    WindowState sunroofState  = WINDOW_UNKNOWN;

    // Other
    AlarmState alarmState   = ALARM_DISARMED;
    bool       mirrorFolded = false;
    uint8_t    errorCount   = 0;
};

// Persisted settings (NVS)
struct AppConfig {
    // WiFi
    char wifiSSID[32]     = "";
    char wifiPassword[64] = "";
    bool wifiEnabled       = true;

    // Web
    char webUser[16]           = WEB_DEFAULT_USER;
    char webPass[16]           = WEB_DEFAULT_PASS;
    bool forcePasswordChange   = true;  // Stays true until the user changes the password

    // Vehicle
    char     vehicleBrand[32] = "Generic";
    char     vehicleModel[32] = "OBD-II";
    uint16_t vehicleYear       = 2020;

    // Display
    ThemeMode theme            = THEME_AUTO;
    uint8_t   brightnessDay     = TFT_BRIGHTNESS_DAY;
    uint8_t   brightnessNight   = TFT_BRIGHTNESS_NIGHT;

    // CAN
    uint32_t canSpeed       = CAN_SPEED;
    bool     listenOnlyMode = true;   // Safe default: listen-only, nothing transmitted on the bus

    // Power
    uint32_t sleepTimeout = AUTO_SLEEP_TIMEOUT;

    // Touch calibration - output of TFT_eSPI's calibrateTouch(): 5
    // uint16_t values mapping raw ADC coordinates to screen pixels.
    uint16_t touchCalData[5] = {0, 0, 0, 0, 0};
    bool     touchCalibrated  = false;  // false = not yet calibrated; first boot should run the wizard

    uint32_t configMagic = 0xCAFE1234;  // Validity marker
};

// ============================================================================
// Configuration management
// ============================================================================

/** Loads settings from NVS; falls back to defaults if invalid/absent. */
bool loadConfig();

/** Saves the current settings to NVS. */
bool saveConfig();

/** Returns a pointer to the current settings, loading them if needed. */
AppConfig* getConfig();

/** Resets settings to factory defaults. */
void setDefaultConfig();

// ============================================================================
// Password / session helpers
// ============================================================================

/** True if the device is still using the default/temporary web password. */
bool isUsingDefaultPassword();

/**
 * Sets a new web password (min 8 characters, must differ from the
 * default). Returns true on success.
 */
bool setWebPassword(const char* newUser, const char* newPass);

/**
 * Callback type invoked whenever setWebPassword() succeeds, regardless
 * of which interface (TFT or web) called it. Lets any module that keeps
 * its own session state (currently WebServerManager) invalidate stale
 * sessions immediately rather than only after a full reboot.
 *
 * Only one callback is supported at a time - sufficient for the current
 * architecture, where WebServerManager is the only session holder.
 */
typedef void (*PasswordChangeCallback)();

/** Registers the callback described above. */
void registerPasswordChangeCallback(PasswordChangeCallback cb);

#endif // CONFIG_H
