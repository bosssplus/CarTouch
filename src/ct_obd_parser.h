#ifndef CT_OBD_PARSER_H
#define CT_OBD_PARSER_H

#include <stdint.h>

struct CtObdSingleFrame {
    uint8_t payloadLength = 0; // bytes after PCI
    uint8_t service = 0;
    uint8_t pid = 0;
    uint8_t dataOffset = 0;
};

static inline bool ctIsObdReplyFrame(uint32_t canId, bool isExtended, bool isRemote) {
    return !isExtended && !isRemote && (canId == 0x7E8u || canId == 0x7E9u);
}

// Validates an ISO-TP single-frame OBD response. The frame layout is:
// [PCI length][service][optional PID][payload...][padding].
// expectedPid may be 0xFF when no PID is expected (e.g. Mode 03 DTC).
static inline bool ctParseObdSingleFrame(const uint8_t* frame, uint8_t dlc,
                                         uint8_t expectedService, uint8_t expectedPid,
                                         CtObdSingleFrame& out) {
    if (!frame || dlc < 2) return false;
    const uint8_t pci = frame[0];
    if ((pci & 0xF0u) != 0x00u) return false; // single-frame only
    const uint8_t payloadLen = (uint8_t)(pci & 0x0Fu);
    if (payloadLen == 0 || payloadLen > 7) return false;
    if ((uint16_t)payloadLen + 1u > dlc) return false;
    if (frame[1] != expectedService) return false;

    uint8_t offset = 2;
    if (expectedPid != 0xFFu) {
        if (payloadLen < 2 || frame[2] != expectedPid) return false;
        offset = 3;
    }
    out.payloadLength = payloadLen;
    out.service = frame[1];
    out.pid = (expectedPid == 0xFFu) ? 0 : frame[2];
    out.dataOffset = offset;
    return true;
}

static inline bool ctDtcPayloadHasValidPairLength(uint8_t payloadLength) {
    // payloadLength includes the positive 0x43 service byte. Remaining bytes
    // must be an integral number of 2-byte DTCs.
    return payloadLength >= 1 && ((payloadLength - 1u) % 2u) == 0u;
}

#endif
