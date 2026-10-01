#ifndef CT_VERIFY_H
#define CT_VERIFY_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline uint32_t ctVerifyFingerprint(uint8_t profileId, uint32_t profileRevision,
                                           const char* label, uint32_t canId, bool extended,
                                           uint8_t length, const uint8_t* data) {
    // FNV-1a over all command identity fields that affect the CAN frame and
    // the profile generation. This prevents a delete/recreate race even if
    // the replacement command happens to have identical CAN bytes.
    uint32_t h = 2166136261u;
    auto add = [&h](uint8_t b) { h ^= b; h *= 16777619u; };
    add(profileId);
    add((uint8_t)profileRevision); add((uint8_t)(profileRevision >> 8));
    add((uint8_t)(profileRevision >> 16)); add((uint8_t)(profileRevision >> 24));
    if (label) for (const char* p = label; *p; ++p) add((uint8_t)*p);
    add(0);
    add((uint8_t)(canId)); add((uint8_t)(canId >> 8));
    add((uint8_t)(canId >> 16)); add((uint8_t)(canId >> 24));
    add(extended ? 1 : 0);
    add(length);
    for (uint8_t i = 0; i < length; ++i) add(data ? data[i] : 0);
    return h == 0 ? 1 : h;
}

static inline bool ctVerifyTransactionValid(bool active, uint8_t pendingProfileId,
                                            const char* pendingLabel, uint32_t pendingToken,
                                            uint32_t pendingAt, uint8_t profileId,
                                            const char* label, uint32_t token,
                                            uint32_t now, uint32_t timeoutMs) {
    if (!active || !pendingLabel || !label || pendingToken == 0 || token == 0) return false;
    if ((uint32_t)(now - pendingAt) > timeoutMs) return false;
    if (profileId != pendingProfileId || token != pendingToken) return false;
    if (strlen(label) >= 32) return false;
    return strcmp(label, pendingLabel) == 0;
}

#endif
