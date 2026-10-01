#ifndef MODULE_STATUS_H
#define MODULE_STATUS_H

#include <Arduino.h>
#include "config.h"

enum ModuleId : uint8_t {
    MODULE_WIFI = 0,
    MODULE_WEB,
    MODULE_CAN,
    MODULE_OBD,
    MODULE_TOUCH,
    MODULE_DISPLAY,
    MODULE_BLE,
    MODULE_STORAGE,
    MODULE_CAN1,
    MODULE_COUNT
};

enum ModuleState : uint8_t {
    MODULE_DETECTED = 0,
    MODULE_INITIALIZING,
    MODULE_READY,
    MODULE_NOT_PRESENT,
    MODULE_ERROR,
    MODULE_DISABLED
};

struct ModuleStatus {
    ModuleState state = MODULE_NOT_PRESENT;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

// RGB outputs are intentionally disabled by default because the physical
// LED wiring/pin map is board-specific. Set these to the three GPIOs of each
// external 4-pin or 6-pin RGB LED after wiring the LEDs. Never assign a GPIO
// already used by the TFT, CAN, touch, USB, flash, or PSRAM interface.
#define RGB_COMMON_ANODE 0
#define RGB_COMMON_CATHODE 1
#define RGB_LED_POLARITY RGB_COMMON_CATHODE

#define RGB_WIFI_R  -1
#define RGB_WIFI_G  -1
#define RGB_WIFI_B  -1
#define RGB_WEB_R   -1
#define RGB_WEB_G   -1
#define RGB_WEB_B   -1
#define RGB_CAN_R   -1
#define RGB_CAN_G   -1
#define RGB_CAN_B   -1
#define RGB_OBD_R   -1
#define RGB_OBD_G   -1
#define RGB_OBD_B   -1
#define RGB_TOUCH_R -1
#define RGB_TOUCH_G -1
#define RGB_TOUCH_B -1

class ModuleStatusManager {
public:
    ModuleStatusManager();
    void begin();
    void setState(ModuleId id, ModuleState state);
    ModuleState getState(ModuleId id) const;
    const ModuleStatus& get(ModuleId id) const;
    const char* name(ModuleId id) const;
    const char* stateText(ModuleState state) const;
    void updateRGB(ModuleId id);

private:
    ModuleStatus _status[MODULE_COUNT];
    int8_t _pins[MODULE_COUNT][3];
    void _setRGBPins(ModuleId id, int8_t r, int8_t g, int8_t b);
    void _writePin(int8_t pin, uint8_t value);
};

#endif
