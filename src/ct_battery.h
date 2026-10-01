#ifndef CT_BATTERY_H
#define CT_BATTERY_H

static inline bool ctBatteryVoltageAvailable(float voltage) {
    return voltage >= 6.0f && voltage <= 36.0f;
}

#endif