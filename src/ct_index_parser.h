#ifndef CT_INDEX_PARSER_H
#define CT_INDEX_PARSER_H

#include <stdint.h>
#include <string.h>

static inline bool ctParseUnsignedDecimal(const char* text, uint32_t maximum,
                                          uint32_t& value) {
    if (!text || !*text) return false;

    uint32_t parsed = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        const uint32_t digit = (uint32_t)(*p - '0');
        if (digit > maximum || parsed > (maximum - digit) / 10u) return false;
        parsed = parsed * 10u + digit;
    }

    value = parsed;
    return true;
}

static inline bool ctParseBoolean(const char* text, bool& value) {
    if (!text) return false;
    if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0) {
        value = true;
        return true;
    }
    if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0) {
        value = false;
        return true;
    }
    return false;
}

static inline bool ctParseBoundedIndex(const char* text, uint8_t limit,
                                       uint8_t& index) {
    if (limit == 0) return false;
    uint32_t value = 0;
    if (!ctParseUnsignedDecimal(text, (uint32_t)limit - 1u, value)) return false;
    index = (uint8_t)value;
    return true;
}

#endif