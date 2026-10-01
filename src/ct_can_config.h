#ifndef CT_CAN_CONFIG_H
#define CT_CAN_CONFIG_H

#include <stdint.h>

static inline bool ctCanPinsConflictFree(uint8_t can0Tx, uint8_t can0Rx,
                                         uint8_t can1Cs, uint8_t can1Int) {
    return can0Tx != can0Rx && can1Cs != can1Int &&
           can0Tx != can1Cs && can0Tx != can1Int &&
           can0Rx != can1Cs && can0Rx != can1Int;
}

static inline bool ctMcp2515BitrateValid(uint32_t bitrate) {
    switch (bitrate) {
        case 100000:
        case 125000:
        case 250000:
        case 500000:
        case 1000000:
            return true;
        default:
            return false;
    }
}

#endif