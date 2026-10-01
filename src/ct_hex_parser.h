#ifndef CT_HEX_PARSER_H
#define CT_HEX_PARSER_H

#include <stdint.h>
#include <stddef.h>

// Strict, allocation-free hexadecimal parsing shared by Web and TFT.
static inline bool ctParseHexUint32(const char* text, uint32_t& value, size_t maxDigits) {
    if (!text || !*text || maxDigits == 0) return false;
    const char* digits = text;
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) digits += 2;
    if (!*digits) return false;
    size_t count = 0;
    uint32_t result = 0;
    for (const char* p = digits; *p; ++p) {
        if (++count > maxDigits) return false;
        uint8_t digit;
        if (*p >= '0' && *p <= '9') digit = (uint8_t)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') digit = (uint8_t)(*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F') digit = (uint8_t)(*p - 'A' + 10);
        else return false;
        if (result > 0x0FFFFFFFu) return false;
        result = (result << 4) | digit;
    }
    value = result;
    return true;
}

static inline bool ctParseHexByteToken(const char* token, uint8_t& value) {
    if (!token || !*token) return false;
    uint32_t parsed = 0;
    if (!ctParseHexUint32(token, parsed, 2)) return false;
    value = (uint8_t)parsed;
    return true;
}

#endif
