#include "recognition/http.h"
#include "platform/network/socket_mode.h"
#include "platform/network/socket_error.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#define HTTP_STATUS_OFFSET    9
#define HTTP_STATUS_END       12
#define HTTP_LAST_STATUS      599
#define DECIMAL_BASE          10
#define HTTP_REQUIRED_HEADERS (2 | 4)
#define HTTP_RETRY_NAME_BYTES (sizeof("Retry-After:") - 1)

/**
 * @brief Check whether a socket operation can be retried asynchronously.
 * @param http Exchange containing the socket.
 * @return Nonzero for no pending socket error or a transient connection condition.
 */
static int pending(CdLookupHttp* http)
{
    int code = platform_socket_error(http->fd);

    return !code || code == EAGAIN || code == EWOULDBLOCK || code == EINTR || code == EINPROGRESS || code == EALREADY || code == ENOTCONN;
}

/**
 * @brief Latch an HTTP failure while preserving a more specific existing failure code.
 * @param http Exchange to mark failed.
 * @return Minus one.
 */
static int fail(CdLookupHttp* http)
{
    if (!http->failed)
    {
        http->failed = 1;
    }

    return -1;
}

void cd_lookup_http_close(CdLookupHttp* http)
{
    if (http->active && http->fd >= 0)
    {
        close(http->fd);
    }

    free(http->data);
    memset(http, 0, sizeof(*http));

    http->fd = -1;
}

int cd_lookup_http_open(CdLookupHttp* http, const char* host, uint32_t address, const char* path, const char* body, uint32_t budget, uint32_t now)
{
    cd_lookup_http_close(http);

    http->active       = 1;
    http->started      = now;
    http->budget       = budget;
    http->image        = !strncmp(path, "/covers/", 8);
    const char* method = body ? "POST" : "GET";
    int         n      = snprintf(http->request, sizeof(http->request), "%s %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\nAccept: %s\r\nAccept-Encoding: identity\r\nUser-Agent: stroom-ps2/2\r\n", method, path, host, CD_LOOKUP_PORT, http->image ? "image/jpeg" : "application/json");

    if (n < 0 || (unsigned)n >= sizeof(http->request))
    {
        return fail(http);
    }

    unsigned used = (unsigned)n;
    n             = body ? snprintf(http->request + used, sizeof(http->request) - used, "Content-Type: application/json\r\nContent-Length: %u\r\n\r\n%s", (unsigned)strlen(body), body) : snprintf(http->request + used, sizeof(http->request) - used, "\r\n");

    if (n < 0 || (unsigned)n >= sizeof(http->request) - used)
    {
        return fail(http);
    }

    http->request_size = used + (unsigned)n;
    http->fd           = socket(AF_INET, SOCK_STREAM, 0);

    if (http->fd < 0 || platform_socket_nonblocking(http->fd))
    {
        return fail(http);
    }

    struct sockaddr_in destination = { 0 };
    destination.sin_family         = AF_INET;
    destination.sin_port           = htons(CD_LOOKUP_PORT);
    destination.sin_addr.s_addr    = htonl(address);

    if (connect(http->fd, (struct sockaddr*)&destination, sizeof(destination)) < 0 && !pending(http))
    {
        return fail(http);
    }

    return 1;
}

/**
 * @brief Parse an unsigned decimal field within an explicit maximum.
 * @param text Nonempty null-terminated decimal text.
 * @param max Maximum accepted value.
 * @param out Receives the value only on success.
 * @return One on success, zero on malformed input or overflow.
 */
static int decimal(const char* text, unsigned max, unsigned* out)
{
    if (!*text)
    {
        return 0;
    }

    unsigned result = 0;

    for (; *text; ++text)
    {
        if (*text < '0' || *text > '9' || result > (max - (unsigned)(*text - '0')) / DECIMAL_BASE)
        {
            return 0;
        }

        result = result * DECIMAL_BASE + (unsigned)(*text - '0');
    }

    *out = result;

    return 1;
}

/**
 * @brief Validate complete HTTP headers and allocate a bounded response body.
 * @param http Exchange header buffer, parsed in place.
 * @return One on success, zero on invalid framing, unsupported content or allocation failure.
 */
static int headers(CdLookupHttp* http)
{
    char* end = strstr(http->header, "\r\n");

    if (!end || (strncmp(http->header, "HTTP/1.1 ", HTTP_STATUS_OFFSET) != 0) || end - http->header < HTTP_STATUS_END)
    {
        return 0;
    }

    unsigned status = 0;
    char     code[4];

    memcpy(code, http->header + HTTP_STATUS_OFFSET, 3);

    code[3] = 0;

    if (!decimal(code, HTTP_LAST_STATUS, &status) || status < LOOKUP_HTTP_OK || (http->header[HTTP_STATUS_END] != ' ' && http->header[HTTP_STATUS_END] != '\r'))
    {
        return 0;
    }

    http->status  = (int)status;
    unsigned seen = 0;

    for (const char* retry = end + 2; retry && *retry;)
    {
        const char* next = strstr(retry, "\r\n");

        if (!next)
        {
            break;
        }

        if (!strncasecmp(retry, "Retry-After:", HTTP_RETRY_NAME_BYTES))
        {
            const char* value = retry + HTTP_RETRY_NAME_BYTES;

            while (value < next && (*value == ' ' || *value == '\t'))
            {
                ++value;
            }

            const char* tail = next;

            while (tail > value && (tail[-1] == ' ' || tail[-1] == '\t'))
            {
                --tail;
            }

            char     number[16];
            unsigned seconds = 0;
            size_t   n       = (size_t)(tail - value);

            if (n < sizeof(number))
            {
                memcpy(number, value, n);

                number[n] = 0;

                if (decimal(number, CD_LOOKUP_MAX_RETRY_SECONDS, &seconds) && seconds > http->retry_seconds)
                {
                    http->retry_seconds = seconds;
                }
            }
        }

        retry = next + 2;
    }

    char* line = end + 2;

    while (*line)
    {
        end = strstr(line, "\r\n");

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

        if (!colon || line[0] == ' ' || line[0] == '\t')
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

        if (!strcasecmp(line, "Retry-After"))
        {
            unsigned retry = 0;

            if (!(seen & 1) && decimal(colon, CD_LOOKUP_MAX_RETRY_SECONDS, &retry) && retry)
            {
                http->retry_seconds = retry;
            }

            seen |= 1;
        }
        else if (!strcasecmp(line, "Content-Length"))
        {
            unsigned max = http->image && http->status == LOOKUP_HTTP_OK ? CD_LOOKUP_COVER_BYTES : CD_LOOKUP_RESPONSE_BYTES;

            if ((seen & 2) || !decimal(colon, max, &http->length) || !http->length)
            {
                if (http->image && http->status == LOOKUP_HTTP_OK)
                {
                    http->failed = 2;
                }

                return 0;
            }

            seen |= 2;
        }
        else if (!strcasecmp(line, "Content-Type"))
        {
            if (seen & 4)
            {
                return 0;
            }

            if (http->image && http->status == LOOKUP_HTTP_OK)
            {
                if ((strcasecmp(colon, "image/jpeg") != 0))
                {
                    http->failed = 2;

                    return 0;
                }
            }
            else if ((strcasecmp(colon, "application/json") != 0) && (strcasecmp(colon, "application/json; charset=utf-8") != 0))
            {
                return 0;
            }

            seen |= 4;
        }
        else if (!strcasecmp(line, "Transfer-Encoding") || (!strcasecmp(line, "Content-Encoding") && (strcasecmp(colon, "identity") != 0)))
        {
            return 0;
        }

        line = end + 2;
    }

    if ((seen & HTTP_REQUIRED_HEADERS) != HTTP_REQUIRED_HEADERS)
    {
        return 0;
    }

    http->data = malloc(http->length);

    return http->data != NULL;
}

int cd_lookup_http_step(CdLookupHttp* http, uint32_t now)
{
    if (!http->active || http->failed)
    {
        return -1;
    }

    if ((uint32_t)(now - http->started) >= http->budget)
    {
        return fail(http);
    }

    if (http->sent < http->request_size)
    {
        int result = send(http->fd, http->request + http->sent, http->request_size - http->sent, 0);

        if (result > 0)
        {
            http->sent += (unsigned)result;
        }
        else if (!result || !pending(http))
        {
            return fail(http);
        }

        return 1;
    }

    unsigned char bytes[4096];

    /* Bounded work per step, yielding to audio between bursts. */

    for (unsigned read = 0; read < 4; ++read)
    {
        int result = recv(http->fd, bytes, sizeof(bytes), 0);

        if (!result)
        {
            return http->data && http->used == http->length ? 0 : fail(http);
        }

        if (result < 0)
        {
            return pending(http) ? 1 : fail(http);
        }

        unsigned at = 0;

        while (at < (unsigned)result && !http->data)
        {
            if (http->header_used >= ARTWORK_HEADER_BYTES || !bytes[at])
            {
                return fail(http);
            }

            http->header[http->header_used++] = (char)bytes[at++];
            http->header[http->header_used]   = 0;

            if (http->header_used >= 4 && !memcmp(http->header + http->header_used - 4, "\r\n\r\n", 4) && !headers(http))
            {
                return fail(http);
            }
        }

        if (http->data)
        {
            unsigned n = (unsigned)result - at;

            if (n > http->length - http->used)
            {
                return fail(http);
            }

            memcpy(http->data + http->used, bytes + at, n);

            http->used += n;

            if (http->used == http->length)
            {
                return 0;
            }
        }

        if ((unsigned)result < sizeof(bytes))
        {
            break;
        }
    }

    return 1;
}
