#include "audio/common/metadata_json.h"
#include <stdint.h>
#include <string.h>

#define JSON_DEPTH_LIMIT            16
#define HEX_DIGIT_MASK              0x0f
#define HEX_ALPHA_OFFSET            10
#define UTF16_SURROGATE_BITS        10
#define UTF8_PAYLOAD_BITS           6
#define UTF8_PAYLOAD_MASK           63
#define ASCII_DELETE                127
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

/**
 * @brief Cursor over a bounded JSON document.
 */
enum
{
    JSON_INVALID_HEX = -1
};

typedef struct
{
    const unsigned char* at;  /**< Next unread byte. */
    const unsigned char* end; /**< End of document. */
} Json;

/**
 * @brief Skip JSON whitespace.
 * @param j Parser cursor.
 */
static void space(Json* j)
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
static int take(Json* j, unsigned char c)
{
    space(j);

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
static int hex4(Json* j)
{
    int value = 0;

    for (unsigned i = 0; i < 4; ++i)
    {
        if (j->at == j->end)
        {
            return JSON_INVALID_HEX;
        }

        unsigned c     = *j->at++;
        int      digit = c >= '0' && c <= '9' ? (int)c - '0' : c >= 'a' && c <= 'f' ? (int)c - 'a' + HEX_ALPHA_OFFSET
                                                           : c >= 'A' && c <= 'F'   ? (int)c - 'A' + HEX_ALPHA_OFFSET
                                                                                    : JSON_INVALID_HEX;

        if (digit < 0)
        {
            return JSON_INVALID_HEX;
        }

        value = value * 16 + digit;
    }

    return value;
}

/**
 * @brief Decode and validate a string, truncating only at UTF-8 boundaries.
 * @param j Parser cursor.
 * @param out Optional destination; control characters become spaces.
 * @param capacity Destination bytes, including terminator.
 * @return One on success, two if truncated, zero for invalid JSON.
 */
static int string(Json* j, char* out, size_t capacity)
{
    if (!take(j, '"'))
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
                c = ' ';
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

        if (c < 32 || c == ASCII_DELETE)
        {
            c = ' ';
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
static int literal(Json* j, const char* word)
{
    space(j);

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
static int skip(Json* j, unsigned depth)
{
    space(j);

    if (depth > JSON_DEPTH_LIMIT || j->at == j->end)
    {
        return 0;
    }

    unsigned c = *j->at;

    if (c == '"')
    {
        return string(j, NULL, 0);
    }

    if (c == '{' || c == '[')
    {
        ++j->at;

        unsigned char end = c == '{' ? '}' : ']';

        if (take(j, end))
        {
            return 1;
        }

        do
        {
            if ((c == '{' && (!string(j, NULL, 0) || !take(j, ':'))) || !skip(j, depth + 1))
            {
                return 0;
            }

            if (take(j, end))
            {
                return 1;
            }
        } while (take(j, ','));
        return 0;
    }

    if (c == 't' || c == 'f' || c == 'n')
    {
        return literal(j, c == 't' ? "true" : c == 'f' ? "false"
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

/**
 * @brief Read supported fields from an object, optionally unwrapping data.
 * @param j Parser cursor.
 * @param metadata Pending labels.
 * @param type Destination request type.
 * @param root Nonzero for the outer object.
 * @param seen Fields already supplied anywhere in this update.
 * @return Nonzero on valid input.
 */
static int object(Json* j, TrackMetadata* metadata, char* type, int root, unsigned* seen)
{
    if (!take(j, '{'))
    {
        return 0;
    }

    if (take(j, '}'))
    {
        return 1;
    }

    do
    {
        char key[32];

        if (!string(j, key, sizeof(key)) || !take(j, ':'))
        {
            return 0;
        }

        unsigned field = !strcmp(key, "title") ? 1 : !strcmp(key, "artist")     ? 2
                                                 : !strcmp(key, "album")        ? 4
                                                 : root && !strcmp(key, "data") ? 8
                                                 : root && !strcmp(key, "type") ? 16
                                                                                : 0;

        if (!strcmp(key, "artworkUrl") || !strcmp(key, "artwork_url"))
        {
            field = 32;
        }

        if (field && (*seen & field))
        {
            return 0;
        }

        *seen |= field;

        if (field == 8)
        {
            if (!object(j, metadata, type, 0, seen))
            {
                return 0;
            }
        }
        else if (field == 16)
        {
            if (!string(j, type, 16))
            {
                return 0;
            }
        }
        else if (field)
        {
            char* out = field == 32 ? metadata->artwork_url : field == 1 ? metadata->title
                                                          : field == 2   ? metadata->artist
                                                                         : metadata->album;

            if (literal(j, "null"))
            {
                out[0] = 0;
            }
            else if (field == 32 ? string(j, out, METADATA_ARTWORK_URL_BYTES) != 1 : !string(j, out, METADATA_TEXT_BYTES))
            {
                return 0;
            }
        }
        else if (!skip(j, 1))
        {
            return 0;
        }

        if (take(j, '}'))
        {
            return 1;
        }
    } while (take(j, ','));

    return 0;
}

TrackMetadataAction track_metadata_apply_json(TrackMetadata* metadata, const void* json, size_t size)
{
    Json          j        = { json, (const unsigned char*)json + size };
    TrackMetadata pending  = *metadata;
    char          type[16] = "";

    unsigned seen = 0;

    if (!object(&j, &pending, type, 1, &seen))
    {
        return TRACK_METADATA_INVALID;
    }

    space(&j);

    if (j.at != j.end)
    {
        return TRACK_METADATA_INVALID;
    }

    if (!strcmp(type, "get"))
    {
        return TRACK_METADATA_GET;
    }

    if (!strcmp(type, "clear"))
    {
        memset(metadata, 0, sizeof(*metadata));
        return TRACK_METADATA_CLEAR;
    }

    if (type[0] && strcmp(type, "update") != 0 && strcmp(type, "metadata") != 0)
    {
        return TRACK_METADATA_INVALID;
    }

    *metadata = pending;

    return TRACK_METADATA_UPDATE;
}

/**
 * @brief Append a label, escaping JSON controls without temporary label buffers.
 * @param out Destination with room for six bytes per input byte.
 * @param text Terminated UTF-8 label.
 * @return Next output position.
 */
static char* escape(char* out, const char* text)
{
    static const char hex[] = "0123456789abcdef";

    while (*text)
    {
        unsigned char c = (unsigned char)*text++;

        if (c < ' ')
        {
            *out++ = '\\';
            *out++ = 'u';
            *out++ = '0';
            *out++ = '0';
            *out++ = hex[c >> 4];
            *out++ = hex[c & HEX_DIGIT_MASK];
        }
        else
        {
            if (c == '"' || c == '\\')
            {
                *out++ = '\\';
            }

            *out++ = (char)c;
        }
    }

    return out;
}

void track_metadata_to_json(const TrackMetadata* metadata, char* json)
{
    const char* labels[]   = { metadata->title, metadata->artist, metadata->album, metadata->artwork_url };
    const char* prefixes[] = { "{\"type\":\"metadata\",\"data\":{\"title\":\"", "\",\"artist\":\"", "\",\"album\":\"", "\",\"artwork_url\":\"" };
    char*       out        = json;

    for (unsigned i = 0; i < sizeof(labels) / sizeof(*labels); ++i)
    {
        size_t size = strlen(prefixes[i]);

        memcpy(out, prefixes[i], size);

        out = escape(out + size, labels[i]);
    }

    strcpy(out, "\"}}");
}
