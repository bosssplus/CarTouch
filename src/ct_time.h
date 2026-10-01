/**
 * ct_time.h - Wrap-safe millisecond helpers
 *
 * millis() wraps every ~49.7 days. Unsigned subtraction stays correct
 * across the wrap as long as the interval being measured is < 2^31 ms.
 */
#ifndef CT_TIME_H
#define CT_TIME_H

#include <stdint.h>

/** true when at least `durationMs` have elapsed since `startMs`. */
static inline bool ctElapsedAtLeast(uint32_t nowMs, uint32_t startMs, uint32_t durationMs) {
    return (uint32_t)(nowMs - startMs) >= durationMs;
}

/** true when MORE than `durationMs` have elapsed since `startMs`. */
static inline bool ctElapsedMoreThan(uint32_t nowMs, uint32_t startMs, uint32_t durationMs) {
    return (uint32_t)(nowMs - startMs) > durationMs;
}

#endif    // CT_TIME_H
