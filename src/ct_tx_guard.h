/**
 * ct_tx_guard.h - Pure CAN TX admission guard
 *
 * The single decision point used by CANManager::sendMessage() before any
 * frame reaches the TWAI driver. Kept free of hardware includes so the
 * Listen-Only protection can be unit-tested natively.
 */
#ifndef CT_TX_GUARD_H
#define CT_TX_GUARD_H

#include <stdint.h>

enum CtTxGuardResult : uint8_t {
    CT_TX_OK = 0,
    CT_TX_ERR_NOT_INITIALIZED,    // driver not installed/started
    CT_TX_ERR_LISTEN_ONLY,        // TWAI actually running in Listen-Only
    CT_TX_ERR_LENGTH              // DLC > 8
};

/**
 * @param initialized       CAN driver is installed and started
 * @param listenOnlyActive  the mode the driver is ACTUALLY running in
 *                          (not merely the configured/requested one)
 * @param length            frame data length (0..8)
 */
static inline CtTxGuardResult ctTxGuard(bool initialized, bool listenOnlyActive,
                                        uint8_t length) {
    if (!initialized)      return CT_TX_ERR_NOT_INITIALIZED;
    if (listenOnlyActive)  return CT_TX_ERR_LISTEN_ONLY;
    if (length > 8)        return CT_TX_ERR_LENGTH;
    return CT_TX_OK;
}

/**
 * true when `id` fits the frame format. The TWAI hardware silently masks an
 * identifier that is too wide (e.g. 0x1234 -> 0x234 for an 11-bit frame),
 * which would put a DIFFERENT frame on the bus than the one requested.
 */
static inline bool ctTxIdValid(uint32_t id, bool isExtended) {
    return isExtended ? (id <= 0x1FFFFFFFUL) : (id <= 0x7FFUL);
}

static inline bool ctSameCanFrameId(uint32_t lhsId, bool lhsExtended,
                                    uint32_t rhsId, bool rhsExtended) {
    return lhsId == rhsId && lhsExtended == rhsExtended;
}

#endif    // CT_TX_GUARD_H
