#include "audio/artwork/http.h"
#include "util/parse.h"
#include "util/ipv4.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <limits.h>

#define HTTP_PORT       80
#define HTTP_SUCCESS    200
#define HTTP_URL_PREFIX "http://"

/**
 * @brief HTTP body framing states.
 */
enum
{
    HEADERS,
    FIXED_BODY,
    CLOSE_BODY,
    CHUNK_SIZE,
    CHUNK_BODY,
    CHUNK_CR,
    CHUNK_LF,
    TRAILERS
};

int artwork_url_parse(const char* url, ArtworkUrl* parsed)
{
    if (strncmp(url, HTTP_URL_PREFIX, sizeof(HTTP_URL_PREFIX) - 1) != 0 || strlen(url) >= METADATA_ARTWORK_URL_BYTES)
    {
        return ARTWORK_HTTP_ERROR_URL;
    }

    const char* at   = url + sizeof(HTTP_URL_PREFIX) - 1;
    const char* host = at;

    if (util_ipv4_parse(&at, &parsed->address) != 0)
    {
        return ARTWORK_HTTP_ERROR_ADDRESS;
    }

    size_t length = (size_t)(at - host);

    memcpy(parsed->host, host, length);

    parsed->host[length] = 0;
    parsed->port         = HTTP_PORT;

    if (*at == ':')
    {
        const char* port = ++at;

        while (*at >= '0' && *at <= '9')
        {
            ++at;
        }

        if ((size_t)(at - port) > UTIL_UINT16_DECIMAL_DIGITS || util_decimal_parse(port, at, UINT16_MAX, &parsed->port) != 0 || !parsed->port)
        {
            return ARTWORK_HTTP_ERROR_PORT;
        }
    }

    if (*at && *at != '/')
    {
        return ARTWORK_HTTP_ERROR_PATH;
    }

    for (const unsigned char* p = (const unsigned char*)at; *p; ++p)
    {
        if (*p <= ' ' || *p >= UTIL_ASCII_DELETE || *p == '#')
        {
            return ARTWORK_HTTP_ERROR_PATH;
        }
    }

    snprintf(parsed->path, sizeof(parsed->path), "%s", *at ? at : "/");

    return 0;
}

/**
 * @brief Parse a bounded decimal length.
 * @param text Decimal digits.
 * @param value Destination length.
 * @return One for a valid size, ARTWORK_HTTP_ERROR_TOO_LARGE if oversized, zero for invalid digits.
 */
static int length_value(const char* text, unsigned* value)
{
    int result = util_decimal_parse(text, text + strlen(text), ARTWORK_MAX_BYTES, value);

    return result == 0 ? 1 : result == UTIL_PARSE_RANGE ? ARTWORK_HTTP_ERROR_TOO_LARGE
                                                        : 0;
}

/**
 * @brief Validate successful response headers and select body framing.
 * @param http Response parser.
 * @return Nonzero for supported framing.
 */
static int headers(ArtworkHttp* http)
{
    int code = 0;

    int parsed = sscanf(http->header, "HTTP/1.%*u %d", &code);

    http->status_code = code;

    if (parsed != 1 || code != HTTP_SUCCESS)
    {
        return 0;
    }

    char* line          = strstr(http->header, "\r\n");
    int   have_length   = 0;
    int   have_encoding = 0;

    while (line && line[2])
    {
        line += 2;

        char* end = strstr(line, "\r\n");

        if (!end)
        {
            return 0;
        }

        if (end == line)
        {
            break;
        }

        *end        = 0;
        char* colon = strchr(line, ':');

        if (!colon)
        {
            return 0;
        }

        *colon++ = 0;

        while (*colon == ' ' || *colon == '\t')
        {
            ++colon;
        }

        char* tail = end;

        while (tail > colon && (tail[-1] == ' ' || tail[-1] == '\t'))
        {
            *--tail = 0;
        }

        if (!strcasecmp(line, "Content-Length"))
        {
            int result = length_value(colon, &http->remaining);

            http->too_large = result < 0;

            if (have_length++ || result != 1)
            {
                return 0;
            }
        }
        else if (!strcasecmp(line, "Transfer-Encoding"))
        {
            if (have_encoding++ || strcasecmp(colon, "chunked") != 0)
            {
                return 0;
            }

            http->chunked = 1;
        }
        else if (!strcasecmp(line, "Content-Encoding") && strcasecmp(colon, "identity") != 0)
        {
            return 0;
        }

        line = end;
    }

    if (have_length && http->chunked)
    {
        return 0;
    }

    http->state       = http->chunked ? CHUNK_SIZE : have_length ? FIXED_BODY
                                                                 : CLOSE_BODY;
    http->header_used = 0;

    return !have_length || http->remaining != 0;
}

int artwork_http_feed(ArtworkHttp* http, const void* data, size_t size, uint8_t* image)
{
    const uint8_t* bytes = data;

    if (http->state < 0)
    {
        return http->state;
    }

    while (size && !http->done)
    {
        unsigned c = *bytes++;

        --size;

        if (http->state == FIXED_BODY || http->state == CLOSE_BODY || http->state == CHUNK_BODY)
        {
            if (http->used == ARTWORK_MAX_BYTES)
            {
                http->too_large = 1;

                goto invalid;
            }

            image[http->used++] = (uint8_t)c;

            if (http->state != CLOSE_BODY && !--http->remaining)
            {
                if (http->state == FIXED_BODY)
                {
                    http->done = 1;
                }
                else
                {
                    http->state = CHUNK_CR;
                }
            }

            continue;
        }

        if (http->state == CHUNK_CR || http->state == CHUNK_LF)
        {
            if (c != (http->state == CHUNK_CR ? '\r' : '\n'))
            {
                goto invalid;
            }

            http->state = http->state == CHUNK_CR ? CHUNK_LF : CHUNK_SIZE;

            continue;
        }

        if (!c || http->header_used + 1 >= sizeof(http->header))
        {
            goto invalid;
        }

        http->header[http->header_used++] = (char)c;
        http->header[http->header_used]   = 0;

        if (http->state == HEADERS)
        {
            if (http->header_used >= 4 && !memcmp(http->header + http->header_used - 4, "\r\n\r\n", 4) && !headers(http))
            {
                goto invalid;
            }
        }
        else if (http->header_used >= 2 && !memcmp(http->header + http->header_used - 2, "\r\n", 2))
        {
            if (http->state == TRAILERS)
            {
                http->done = http->header_used == 2;
            }
            else
            {
                unsigned    length = 0;
                unsigned    digits = 0;
                const char* p      = http->header;

                while (*p != '\r' && *p != ';')
                {
                    unsigned ch    = (unsigned char)*p++;
                    int      digit = util_hex_digit(ch);

                    if (digit < 0 || length > ARTWORK_MAX_BYTES / UTIL_HEX_BASE)
                    {
                        http->too_large = length > ARTWORK_MAX_BYTES / UTIL_HEX_BASE;

                        goto invalid;
                    }

                    length = length * UTIL_HEX_BASE + (unsigned)digit;

                    ++digits;
                }

                if (!digits || length > ARTWORK_MAX_BYTES - http->used)
                {
                    http->too_large = length > ARTWORK_MAX_BYTES - http->used;

                    goto invalid;
                }

                http->remaining = length;
                http->state     = length ? CHUNK_BODY : TRAILERS;
            }

            http->header_used = 0;
        }
    }

    return !http->done ? 1 : http->used ? 0
                                        : ARTWORK_HTTP_ERROR_EMPTY_BODY;
invalid:
    http->state = http->too_large ? ARTWORK_HTTP_ERROR_TOO_LARGE : ARTWORK_HTTP_ERROR_RESPONSE;

    return http->state;
}

int artwork_http_eof(ArtworkHttp* http)
{
    if (http->state == CLOSE_BODY && http->used)
    {
        http->done = 1;
    }

    if (http->state < 0)
    {
        return http->state;
    }

    return http->done && http->used ? 0 : ARTWORK_HTTP_ERROR_INCOMPLETE;
}
