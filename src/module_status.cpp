#include "module_status.h"

ModuleStatusManager::ModuleStatusManager() {
    for (uint8_t i = 0; i < MODULE_COUNT; ++i) {
        _status[i] = ModuleStatus();
        _pins[i][0] = _pins[i][1] = _pins[i][2] = -1;
    }
    _setRGBPins(MODULE_WIFI,  RGB_WIFI_R,  RGB_WIFI_G,  RGB_WIFI_B);
    _setRGBPins(MODULE_WEB,   RGB_WEB_R,   RGB_WEB_G,   RGB_WEB_B);
    _setRGBPins(MODULE_CAN,   RGB_CAN_R,   RGB_CAN_G,   RGB_CAN_B);
    _setRGBPins(MODULE_OBD,   RGB_OBD_R,   RGB_OBD_G,   RGB_OBD_B);
    _setRGBPins(MODULE_TOUCH, RGB_TOUCH_R, RGB_TOUCH_G, RGB_TOUCH_B);
}

void ModuleStatusManager::_setRGBPins(ModuleId id, int8_t r, int8_t g, int8_t b) {
    _pins[id][0] = r;
    _pins[id][1] = g;
    _pins[id][2] = b;
}

void ModuleStatusManager::begin() {
    for (uint8_t i = 0; i < MODULE_COUNT; ++i) {
        for (uint8_t c = 0; c < 3; ++c) {
            if (_pins[i][c] >= 0) {
                pinMode(_pins[i][c], OUTPUT);
                _writePin(_pins[i][c], 0);
            }
        }
        updateRGB((ModuleId)i);
    }
}

void ModuleStatusManager::_writePin(int8_t pin, uint8_t value) {
    if (pin < 0) return;
    const uint8_t off = (RGB_LED_POLARITY == RGB_COMMON_ANODE) ? HIGH : LOW;
    const uint8_t on  = (RGB_LED_POLARITY == RGB_COMMON_ANODE) ? LOW : HIGH;
    digitalWrite(pin, value ? on : off);
}

void ModuleStatusManager::setState(ModuleId id, ModuleState state) {
    if (id >= MODULE_COUNT) return;
    _status[id].state = state;
    switch (state) {
        case MODULE_READY:
            _status[id].r = 0; _status[id].g = 255; _status[id].b = 0;
            break;
        case MODULE_ERROR:
            _status[id].r = 255; _status[id].g = 0; _status[id].b = 0;
            break;
        case MODULE_DISABLED:
            _status[id].r = 0; _status[id].g = 0; _status[id].b = 255;
            break;
        case MODULE_INITIALIZING:
            _status[id].r = 255; _status[id].g = 180; _status[id].b = 0;
            break;
        case MODULE_DETECTED:
            _status[id].r = 0; _status[id].g = 160; _status[id].b = 255;
            break;
        case MODULE_NOT_PRESENT:
        default:
            _status[id].r = 128; _status[id].g = 128; _status[id].b = 128;
            break;
    }
    updateRGB(id);
}

ModuleState ModuleStatusManager::getState(ModuleId id) const {
    return (id < MODULE_COUNT) ? _status[id].state : MODULE_ERROR;
}

const ModuleStatus& ModuleStatusManager::get(ModuleId id) const {
    // Keep the invalid fallback compatible with older C++ standards used by
    // some Arduino-ESP32/PlatformIO toolchains. ModuleStatus has default
    // member initializers, so brace-initializing all four fields here is not
    // portable across the project's supported compiler modes.
    static ModuleStatus invalid;
    invalid.state = MODULE_ERROR;
    invalid.r = 255;
    invalid.g = 0;
    invalid.b = 0;
    return (id < MODULE_COUNT) ? _status[id] : invalid;
}

const char* ModuleStatusManager::name(ModuleId id) const {
    switch (id) {
        case MODULE_WIFI: return "WiFi";
        case MODULE_WEB: return "Web Server";
        case MODULE_CAN: return "CAN Bus";
        case MODULE_OBD: return "OBD-II";
        case MODULE_TOUCH: return "Touch";
        case MODULE_DISPLAY: return "Display";
        case MODULE_BLE: return "BLE";
        case MODULE_STORAGE: return "Storage";
        case MODULE_CAN1: return "CAN1 (MCP2515)";
        default: return "Unknown";
    }
}

const char* ModuleStatusManager::stateText(ModuleState state) const {
    switch (state) {
        case MODULE_READY: return "READY";
        case MODULE_DETECTED: return "DETECTED";
        case MODULE_INITIALIZING: return "INITIALIZING";
        case MODULE_ERROR: return "ERROR";
        case MODULE_DISABLED: return "DISABLED";
        case MODULE_NOT_PRESENT:
        default: return "NOT DETECTED";
    }
}

void ModuleStatusManager::updateRGB(ModuleId id) {
    if (id >= MODULE_COUNT) return;
    const ModuleStatus& s = _status[id];
    _writePin(_pins[id][0], s.r);
    _writePin(_pins[id][1], s.g);
    _writePin(_pins[id][2], s.b);
}
