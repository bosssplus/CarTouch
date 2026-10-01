#ifndef CT_DBC_VALIDATION_H
#define CT_DBC_VALIDATION_H

#include <stdint.h>

// Returns true if every bit occupied by a DBC signal lies inside messageDlc.
// Handles Intel linear bit numbering and Motorola saw-tooth bit numbering.
static inline bool ctDbcSignalFitsDlc(uint8_t startBit, uint8_t length,
                                      bool bigEndian, uint8_t messageDlc) {
    if (length == 0 || length > 64 || startBit >= 64 || messageDlc > 8) return false;
    uint8_t byteIdx = (uint8_t)(startBit / 8);
    int8_t bitIdx = (int8_t)(startBit % 8);
    for (uint8_t i = 0; i < length; ++i) {
        if (byteIdx >= messageDlc) return false;
        if (!bigEndian) {
            ++bitIdx;
            if (bitIdx >= 8) { bitIdx = 0; ++byteIdx; }
        } else {
            --bitIdx;
            if (bitIdx < 0) { bitIdx = 7; ++byteIdx; }
        }
    }
    return true;
}

// Decode a DBC message ID into the arbitration ID used by the CAN controller
// and the frame format. The DBC convention sets bit 31 for extended frames.
// Some bundled/vendor DBCs omit that marker and store a raw ID above 0x7FF;
// those are treated as extended as well, because a standard CAN frame cannot
// legally carry an identifier larger than 11 bits.
static inline bool ctDecodeDbcCanId(uint32_t rawId, uint32_t& canId, bool& isExtended) {
    if (rawId & 0x80000000UL) {
        canId = rawId & 0x1FFFFFFFUL;
        if (canId > 0x1FFFFFFFUL) return false;
        isExtended = true;
        return true;
    }
    if (rawId <= 0x7FFUL) {
        canId = rawId;
        isExtended = false;
        return true;
    }
    // Compatibility with vendor/OpenDBC files that encode a 29-bit ID
    // directly without the DBC extended-ID marker.
    canId = rawId & 0x1FFFFFFFUL;
    if (canId == 0 || canId > 0x1FFFFFFFUL) return false;
    isExtended = true;
    return true;
}

#endif
