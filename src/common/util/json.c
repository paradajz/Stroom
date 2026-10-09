#include "util/json.h"
#include "util/parse.h"
#include <stdint.h>
#include <string.h>

#define JSON_DEPTH_LIMIT            16
#define UTF16_SURROGATE_BITS        10
#define UTF8_PAYLOAD_BITS           6
#define UTF8_PAYLOAD_MASK           63
#define UTF16_HIGH_FIRST            0xd800
#define UTF16_HIGH_LAST             0xdbff
#define UTF16_LOW_FIRST             0xdc00
#define UTF16_LOW_LAST              0xdfff
#define UNICODE_SUPPLEMENTARY_FIRST 0x10000
#define UNICODE_LAST                0x10ffff
#define UTF8_TWO_FIRST              0xc2
#define UTF8_TWO_LAST               0xdf
#define UTF8_THREE_FIRST            0xe0
#define UTF8_THREE_LAST             0xef
#define UTF8_FOUR_FIRST             0xf0
#define UTF8_FOUR_LAST              0xf4
#define UTF8_LEAD_MASK              0xc0
#define UTF8_CONTINUATION           0x80
#define UTF8_THREE_MINIMUM          0x800

enum
{
    JSON_INVALID_HEX = -1
};

/**
 * @brief Skip JSON whitespace.
 * @param j Parser cursor.
 */
void util_json_space(UtilJson* j)
{
    while (j->at < j->end && (*j->at == ' ' || *j->at == '\t' || *j->at == '\r' || *j->at == '\n'))
    {
        ++j->at;
    }
}

/**
 * @brief Consume a required punctuation byte after whitespace.
 * @param j Parser cursor.
 * @param c Expected byte.
 * @return Nonzero on a match.
 */
int util_json_take(UtilJson* j, unsigned char c)
{
    util_json_space(j);

    if (j->at == j->end || *j->at != c)
    {
        return 0;
    }

    ++j->at;

    return 1;
}

/**
 * @brief Decode four hexadecimal digits.
 * @param j Parser cursor.
 * @return Code unit, or minus one on malformed input.
 */
static int hex4(UtilJson* j)
{
    int value = 0;

    for (unsigned i = 0; i < 4; ++i)
    {
        if (j->at == j->end)
        {
            return JSON_INVALID_HEX;
        }

        unsigned c     = *j->at++;
        int      digit = util_hex_digit(c);

        if (digit < 0)
        {
            return JSON_INVALID_HEX;
        }

        value = value * UTIL_HEX_BASE + digit;
    }

    return value;
}

/**
 * @brief Decode and validate a string, truncating only at UTF-8 boundaries.
 * @param j Parser cursor.
 * @param out Optional destination; text handling follows the cursor policy.
 * @param capacity Destination bytes, including terminator.
 * @return One on success, two if truncated, zero for invalid JSON.
 */
int util_json_string(UtilJson* j, char* out, size_t capacity)
{
    if ((out && !capacity) || !util_json_take(j, '"'))
    {
        return 0;
    }

    size_t used = 0;
    int    full = 0;

    while (j->at < j->end)
    {
        uint32_t c = *j->at++;

        if (c == '"')
        {
            if (out)
            {
                out[used] = 0;
            }

            return full ? 2 : 1;
        }

        if (c < 32)
        {
            return 0;
        }

        if (c == '\\')
        {
            if (j->at == j->end)
            {
                return 0;
            }

            c = *j->at++;

            if (c == 'u')
            {
                int unit = hex4(j);

                if (unit < 0)
                {
                    return 0;
                }

                c = (uint32_t)unit;

                if (c >= UTF16_HIGH_FIRST && c <= UTF16_HIGH_LAST)
                {
                    if (j->end - j->at < 2 || j->at[0] != '\\' || j->at[1] != 'u')
                    {
                        return 0;
                    }

                    j->at += 2;

                    int low = hex4(j);

                    if (low < UTF16_LOW_FIRST || low > UTF16_LOW_LAST)
                    {
                        return 0;
                    }

                    c = UNICODE_SUPPLEMENTARY_FIRST + ((c - UTF16_HIGH_FIRST) << UTF16_SURROGATE_BITS) + (uint32_t)(low - UTF16_LOW_FIRST);
                }
                else if (c >= UTF16_LOW_FIRST && c <= UTF16_LOW_LAST)
                {
                    return 0;
                }
            }
            else if (c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't')
            {
                c = c == 'b' ? 8 : c == 'f' ? '\f'
                               : c == 'n'   ? '\n'
                               : c == 'r'   ? '\r'
                                            : '\t';
            }
            else if (c != '"' && c != '\\' && c != '/')
            {
                return 0;
            }
        }
        else if (c >= 128)
        {
            unsigned n       = c >= UTF8_TWO_FIRST && c <= UTF8_TWO_LAST ? 1 : c >= UTF8_THREE_FIRST && c <= UTF8_THREE_LAST ? 2
                                                                           : c >= UTF8_FOUR_FIRST && c <= UTF8_FOUR_LAST     ? 3
                                                                                                                             : 0;
            uint32_t minimum = n == 1 ? UTF8_CONTINUATION : n == 2 ? UTF8_THREE_MINIMUM
                                                                   : UNICODE_SUPPLEMENTARY_FIRST;

            if (!n || (size_t)(j->end - j->at) < n)
            {
                return 0;
            }

            c &= (1u << (UTF8_PAYLOAD_BITS - n)) - 1;

            for (unsigned i = 0; i < n; ++i)
            {
                unsigned byte = *j->at++;

                if ((byte & UTF8_LEAD_MASK) != UTF8_CONTINUATION)
                {
                    return 0;
                }

                c = (c << UTF8_PAYLOAD_BITS) | (byte & UTF8_PAYLOAD_MASK);
            }

            if (c < minimum || c > UNICODE_LAST || (c >= UTF16_HIGH_FIRST && c <= UTF16_LOW_LAST))
            {
                return 0;
            }
        }

        if (j->text_mode == UTIL_JSON_TEXT_DISPLAY)
        {
            if (c < 32 || c == UTIL_ASCII_DELETE)
            {
                c = ' ';
            }
        }
        else if (!c)
        {
            return 0;
        }

        unsigned n = c < 128 ? 1 : c < UTF8_THREE_MINIMUM        ? 2
                               : c < UNICODE_SUPPLEMENTARY_FIRST ? 3
                                                                 : 4;

        if (out && !full && used + n < capacity)
        {
            if (n == 1)
            {
                out[used++] = (char)c;
            }
            else
            {
                out[used++] = (char)((n == 2 ? UTF8_LEAD_MASK : n == 3 ? UTF8_THREE_FIRST
                                                                       : UTF8_FOUR_FIRST) |
                                     (c >> (UTF8_PAYLOAD_BITS * (n - 1))));

                for (unsigned i = n - 1; i > 0; --i)
                {
                    out[used++] = (char)(UTF8_CONTINUATION | ((c >> (UTF8_PAYLOAD_BITS * (i - 1))) & UTF8_PAYLOAD_MASK));
                }
            }
        }
        else
        {
            full = 1;
        }
    }

    return 0;
}

/**
 * @brief Consume a fixed JSON literal.
 * @param j Parser cursor.
 * @param word Literal spelling.
 * @return Nonzero on a match.
 */
int util_json_literal(UtilJson* j, const char* word)
{
    util_json_space(j);

    size_t n = strlen(word);

    if ((size_t)(j->end - j->at) < n || memcmp(j->at, word, n) != 0)
    {
        return 0;
    }

    j->at += n;

    return 1;
}

/**
 * @brief Validate and skip an unknown JSON value with bounded nesting.
 * @param j Parser cursor.
 * @param depth Current nesting depth.
 * @return Nonzero on valid input.
 */
int util_json_skip(UtilJson* j, unsigned depth)
{
    util_json_space(j);

    if (depth > JSON_DEPTH_LIMIT || j->at == j->end)
    {
        return 0;
    }

    unsigned c = *j->at;

    if (c == '"')
    {
        return util_json_string(j, NULL, 0);
    }

    if (c == '{' || c == '[')
    {
        ++j->at;

        unsigned char end = c == '{' ? '}' : ']';

        if (util_json_take(j, end))
        {
            return 1;
        }

        do
        {
            if ((c == '{' && (!util_json_string(j, NULL, 0) || !util_json_take(j, ':'))) || !util_json_skip(j, depth + 1))
            {
                return 0;
            }

            if (util_json_take(j, end))
            {
                return 1;
            }
        } while (util_json_take(j, ','));
        return 0;
    }

    if (c == 't' || c == 'f' || c == 'n')
    {
        return util_json_literal(j, c == 't' ? "true" : c == 'f' ? "false"
                                                                 : "null");
    }

    if (c == '-')
    {
        ++j->at;
    }

    if (j->at == j->end || *j->at < '0' || *j->at > '9')
    {
        return 0;
    }

    if (*j->at++ != '0')
    {
        while (j->at < j->end && *j->at >= '0' && *j->at <= '9')
        {
            ++j->at;
        }
    }

    for (unsigned part = 0; part < 2; ++part)
    {
        if (j->at < j->end && (part ? (*j->at == 'e' || *j->at == 'E') : *j->at == '.'))
        {
            ++j->at;

            if (part && j->at < j->end && (*j->at == '+' || *j->at == '-'))
            {
                ++j->at;
            }

            const unsigned char* begin = j->at;

            while (j->at < j->end && *j->at >= '0' && *j->at <= '9')
            {
                ++j->at;
            }

            if (begin == j->at)
            {
                return 0;
            }
        }
    }

    return 1;
}
