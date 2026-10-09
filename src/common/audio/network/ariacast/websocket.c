#include "audio/network/ariacast/websocket.h"
#include <string.h>

#define WS_KEY_BYTES           24
#define WS_KEY_DATA_BYTES      22
#define WS_LENGTH_MASK         0x7f
#define WS_OPCODE_MASK         0x0f
#define WS_RESERVED_BITS       0x70
#define WS_EXTENDED_LENGTH     126
#define WS_LONG_LENGTH         127
#define WS_SHORT_MASKED_HEADER 6
#define SHA1_INITIAL_A         0x67452301u
#define SHA1_INITIAL_B         0xefcdab89u
#define SHA1_INITIAL_C         0x98badcfeu
#define SHA1_INITIAL_D         0x10325476u
#define SHA1_INITIAL_E         0xc3d2e1f0u
#define SHA1_SCHEDULE_TAP      14
#define SHA1_ROUND_GROUP       20
#define SHA1_ROTATE_A          5
#define SHA1_ROTATE_B          30
#define BASE64_BITS            6
#define BASE64_MASK            63
#define SHA1_BLOCK_BYTES       64
#define SHA1_ROUNDS            80
#define SHA1_WORDS             5
#define SHA1_DIGEST_BYTES      20
#define SHA1_FIRST_CONSTANT    0x5a827999u
#define SHA1_SECOND_CONSTANT   0x6ed9eba1u
#define SHA1_THIRD_CONSTANT    0x8f1bbcdcu
#define SHA1_FOURTH_CONSTANT   0xca62c1d6u

/**
 * @brief Rotate a SHA-1 word left.
 * @param value Word.
 * @param bits Rotation.
 * @return Rotated word.
 */
static uint32_t rotate(uint32_t value, unsigned bits)
{
    return (value << bits) | (value >> (32 - bits));
}

int aria_websocket_accept(const char* key, char accept[ARIA_WS_ACCEPT_BYTES])
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const char guid[]     = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    if (strlen(key) != WS_KEY_BYTES || key[WS_KEY_DATA_BYTES] != '=' || key[WS_KEY_BYTES - 1] != '=')
    {
        return ARIA_WEBSOCKET_ERROR_KEY;
    }

    for (unsigned i = 0; i < WS_KEY_DATA_BYTES; ++i)
    {
        if (!strchr(alphabet, key[i]))
        {
            return ARIA_WEBSOCKET_ERROR_KEY;
        }
    }

    if ((strchr(alphabet, key[WS_KEY_DATA_BYTES - 1]) - alphabet) % 16)
    {
        return ARIA_WEBSOCKET_ERROR_KEY;
    }

    uint8_t blocks[2 * SHA1_BLOCK_BYTES] = { 0 };

    memcpy(blocks, key, WS_KEY_BYTES);
    memcpy(blocks + WS_KEY_BYTES, guid, sizeof(guid) - 1);

    unsigned length = WS_KEY_BYTES + sizeof(guid) - 1;

    blocks[length]             = 0x80;
    blocks[sizeof(blocks) - 2] = (uint8_t)((length * 8) >> 8);
    blocks[sizeof(blocks) - 1] = (uint8_t)(length * 8);

    uint32_t hash[SHA1_WORDS] = { SHA1_INITIAL_A, SHA1_INITIAL_B, SHA1_INITIAL_C, SHA1_INITIAL_D, SHA1_INITIAL_E };

    for (unsigned block = 0; block < 2; ++block)
    {
        uint32_t words[SHA1_ROUNDS];

        for (unsigned i = 0; i < 16; ++i)
        {
            const uint8_t* p = blocks + block * SHA1_BLOCK_BYTES + i * 4;

            words[i] = (uint32_t)p[0] << (3 * 8) | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
        }

        for (unsigned i = 16; i < SHA1_ROUNDS; ++i)
        {
            words[i] = rotate(words[i - 3] ^ words[i - 8] ^ words[i - SHA1_SCHEDULE_TAP] ^ words[i - 16], 1);
        }

        uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4];

        for (unsigned i = 0; i < SHA1_ROUNDS; ++i)
        {
            uint32_t f, k;

            if (i < SHA1_ROUND_GROUP)
            {
                f = (b & c) | (~b & d);
                k = SHA1_FIRST_CONSTANT;
            }
            else if (i < 2 * SHA1_ROUND_GROUP)
            {
                f = b ^ c ^ d;
                k = SHA1_SECOND_CONSTANT;
            }
            else if (i < 3 * SHA1_ROUND_GROUP)
            {
                f = (b & c) | (b & d) | (c & d);
                k = SHA1_THIRD_CONSTANT;
            }
            else
            {
                f = b ^ c ^ d;
                k = SHA1_FOURTH_CONSTANT;
            }

            uint32_t t = rotate(a, SHA1_ROTATE_A) + f + e + k + words[i];

            e = d;
            d = c;
            c = rotate(b, SHA1_ROTATE_B);
            b = a;
            a = t;
        }

        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
    }

    uint8_t digest[SHA1_DIGEST_BYTES];

    for (unsigned i = 0; i < sizeof(digest); ++i)
    {
        digest[i] = (uint8_t)(hash[i / 4] >> (3 * 8 - 8 * (i % 4)));
    }

    unsigned out = 0;

    for (unsigned i = 0; i < sizeof(digest); i += 3)
    {
        unsigned remain = sizeof(digest) - i;
        uint32_t value  = (uint32_t)digest[i] << 16;

        if (remain > 1)
        {
            value |= (uint32_t)digest[i + 1] << 8;
        }

        if (remain > 2)
        {
            value |= digest[i + 2];
        }

        accept[out++] = alphabet[value >> (3 * BASE64_BITS)];
        accept[out++] = alphabet[(value >> (2 * BASE64_BITS)) & BASE64_MASK];
        accept[out++] = remain > 1 ? alphabet[(value >> BASE64_BITS) & BASE64_MASK] : '=';
        accept[out++] = remain > 2 ? alphabet[value & BASE64_MASK] : '=';
    }

    accept[out] = 0;

    return 0;
}

/**
 * @brief Validate a collected frame header.
 * @param ws Decoder.
 * @return Nonzero for supported, bounded framing.
 */
static int header(AriaWebSocket* ws)
{
    unsigned code = ws->header[1] & WS_LENGTH_MASK;

    ws->opcode = ws->header[0] & WS_OPCODE_MASK;
    ws->final  = (ws->header[0] & 128) != 0;

    if ((ws->header[0] & WS_RESERVED_BITS) || !(ws->header[1] & 128) || code == WS_LONG_LENGTH)
    {
        return 0;
    }

    ws->remaining = code == WS_EXTENDED_LENGTH ? (unsigned)ws->header[2] * 256 + ws->header[3] : code;

    if (code == WS_EXTENDED_LENGTH && ws->remaining < WS_EXTENDED_LENGTH)
    {
        return 0;
    }

    unsigned op = ws->opcode;

    if (op >= 8)
    {
        if ((op != 8 && op != ARIA_WS_PING && op != ARIA_WS_PONG) || !ws->final || ws->remaining > sizeof(ws->control) || (op == 8 && ws->remaining == 1))
        {
            return 0;
        }

        ws->control_used = 0;
    }
    else
    {
        if (op != 0 && op != 1 && op != 2)
        {
            return 0;
        }

        if ((op == 0 && !ws->message_opcode) || (op != 0 && ws->message_opcode))
        {
            return 0;
        }

        if (op)
        {
            ws->used           = 0;
            ws->message_opcode = op;
        }

        if (ws->remaining > sizeof(ws->data) - ws->used)
        {
            return 0;
        }
    }

    memcpy(ws->mask, ws->header + ws->header_size - 4, 4);

    ws->offset = 0;

    return 1;
}

int aria_websocket_feed(AriaWebSocket* ws, const uint8_t* data, size_t size, AriaMessage message, void* context)
{
    while (size && !ws->closed)
    {
        if (!ws->header_size)
        {
            ws->header_size = 2;
        }

        if (ws->header_used < ws->header_size)
        {
            ws->header[ws->header_used++] = *data++;

            --size;

            if (ws->header_used == 2)
            {
                unsigned length = ws->header[1] & WS_LENGTH_MASK;

                if (length == WS_LONG_LENGTH)
                {
                    return ARIA_WEBSOCKET_ERROR_LENGTH;
                }

                ws->header_size = length == WS_EXTENDED_LENGTH ? ARIA_WS_HEADER_BYTES : WS_SHORT_MASKED_HEADER;
            }

            if (ws->header_used != ws->header_size)
            {
                continue;
            }

            if (!header(ws))
            {
                return ARIA_WEBSOCKET_ERROR_HEADER;
            }
        }

        while (size && ws->remaining)
        {
            uint8_t value = *data++ ^ ws->mask[ws->offset++ % 4];

            --size;
            --ws->remaining;

            if (ws->opcode >= 8)
            {
                ws->control[ws->control_used++] = value;
            }
            else
            {
                ws->data[ws->used++] = value;
            }
        }

        if (!ws->remaining)
        {
            if (ws->opcode >= 8)
            {
                if (!message(context, ws->opcode, ws->control, ws->control_used))
                {
                    return ARIA_WEBSOCKET_ERROR_MESSAGE_REJECTED;
                }

                if (ws->opcode == ARIA_WS_CLOSE)
                {
                    ws->closed = 1;
                }
            }
            else if (ws->final)
            {
                unsigned opcode = ws->message_opcode;

                ws->message_opcode = 0;

                if (!message(context, opcode, ws->data, ws->used))
                {
                    return ARIA_WEBSOCKET_ERROR_MESSAGE_REJECTED;
                }
            }

            ws->header_used = ws->header_size = 0;
        }
    }

    return 0;
}

unsigned aria_websocket_frame(uint8_t* output, unsigned capacity, unsigned opcode, const void* data, unsigned size)
{
    unsigned prefix = size < WS_EXTENDED_LENGTH ? 2 : 4;

    if (size > ARIA_MESSAGE_BYTES || size + prefix > capacity)
    {
        return 0;
    }

    output[0] = (uint8_t)(128 | opcode);
    output[1] = size < WS_EXTENDED_LENGTH ? (uint8_t)size : WS_EXTENDED_LENGTH;

    if (prefix == 4)
    {
        output[2] = (uint8_t)(size >> 8);
        output[3] = (uint8_t)size;
    }

    if (size)
    {
        memcpy(output + prefix, data, size);
    }

    return size + prefix;
}
