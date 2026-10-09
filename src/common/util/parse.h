#pragma once
#include <stddef.h>
#include <stdint.h>

#define UTIL_DECIMAL_BASE          10
#define UTIL_HEX_BASE              16
#define UTIL_HEX_DIGIT_MASK        0x0f
#define UTIL_ASCII_DELETE          127
#define UTIL_UINT16_DECIMAL_DIGITS 5

/** Failure codes for bounded unsigned text parsing. */
typedef enum
{
    UTIL_PARSE_INVALID = -1,
    UTIL_PARSE_RANGE   = -2
} UtilParseError;

/**
 * @brief Decode one ASCII hexadecimal digit, independent of locale.
 * @param c Input byte.
 * @return Value in 0..15, or minus one for a non-hexadecimal byte.
 */
static inline int util_hex_digit(unsigned c)
{
    return c >= '0' && c <= '9' ? (int)c - '0' : c >= 'a' && c <= 'f' ? (int)c - 'a' + UTIL_DECIMAL_BASE
                                             : c >= 'A' && c <= 'F'   ? (int)c - 'A' + UTIL_DECIMAL_BASE
                                                                      : -1;
}

/**
 * @brief Parse a nonempty decimal span without signs, whitespace or overflow.
 * @param begin First digit, including leading zeroes if present.
 * @param end Exclusive end; no terminator is required.
 * @param max Largest accepted value.
 * @param out Destination, written only on success.
 * @return Zero on success, INVALID for empty/non-digit input, RANGE for overflow.
 */
static inline int util_decimal_parse(const char* begin, const char* end, unsigned max, unsigned* out)
{
    if (begin >= end)
    {
        return UTIL_PARSE_INVALID;
    }

    unsigned value = 0;

    for (const char* at = begin; at < end; ++at)
    {
        if (*at < '0' || *at > '9')
        {
            return UTIL_PARSE_INVALID;
        }

        unsigned digit = (unsigned)(*at - '0');

        if (digit > max || value > (max - digit) / UTIL_DECIMAL_BASE)
        {
            return UTIL_PARSE_RANGE;
        }

        value = value * UTIL_DECIMAL_BASE + digit;
    }

    *out = value;

    return 0;
}
