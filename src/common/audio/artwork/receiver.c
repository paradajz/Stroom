#include "audio/artwork/receiver.h"
#include "platform/network/socket_error.h"
#include "platform/time/clock.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ARTWORK_SETTLE_MS      250
#define ARTWORK_POLL_MS        10
#define ARTWORK_TIMEOUT_MS     180000
#define ARTWORK_STALL_MS       5000
#define ARTWORK_RETRY_MS       2000
#define ARTWORK_RETRY_SLOW_MS  30000
#define ARTWORK_ATTEMPTS       3
#define ARTWORK_READ_BYTES     4096
#define ARTWORK_BURST_MS       4
#define ARTWORK_BUFFER_INITIAL 8192

#if STROOM_DIAGNOSTICS
#define ARTWORK_TIMING_BEGIN(name)                          uint32_t name = platform_millis()
#define ARTWORK_TIMING_END(receiver, phase, begin, a, b, c) observe(receiver, phase, begin, a, b, c)
#else
#define ARTWORK_TIMING_BEGIN(name)
#define ARTWORK_TIMING_END(receiver, phase, begin, a, b, c)
#endif

#if STROOM_DIAGNOSTICS
/** @brief Retain a completed operation; elapsed time includes scheduling and RPC waits. */
static void observe(ArtworkReceiver* receiver, ArtworkPhase phase, uint32_t begin, unsigned a, unsigned b, unsigned c)
{
    int      saved_errno = errno;
    uint32_t end         = platform_millis();

    if (receiver->observer)
    {
        receiver->observer(&receiver->blob.metadata, (ArtworkObservation){ phase, begin, end, { a, b, c } });
    }

    errno = saved_errno;
}
#endif

/** @brief Close the current socket, retaining its operation duration. */
static void close_socket(ArtworkReceiver* receiver)
{
    ARTWORK_TIMING_BEGIN(close_begin);

    int result = close(receiver->fd);

    (void)result;
    ARTWORK_TIMING_END(receiver, DIAGNOSTIC_ARTWORK_CLOSE, close_begin, (unsigned)receiver->fd, (unsigned)result, receiver->http.done);
}

/** @brief Release compressed storage without adding allocations to timing collection. */
static void release_data(ArtworkReceiver* receiver)
{
    if (!receiver->blob.data)
    {
        return;
    }

    ARTWORK_TIMING_BEGIN(release_begin);
    free(receiver->blob.data);
    ARTWORK_TIMING_END(receiver, DIAGNOSTIC_ARTWORK_WORKER_RELEASE, release_begin, receiver->capacity, receiver->http.used, 0);
}

void artwork_receiver_close(ArtworkReceiver* receiver)
{
    if (receiver->initialized && receiver->fd >= 0)
    {
        close_socket(receiver);
    }

    release_data(receiver);
#if STROOM_DIAGNOSTICS
    ArtworkObserver observer = receiver->observer;
#endif
    memset(receiver, 0, sizeof(*receiver));
#if STROOM_DIAGNOSTICS
    receiver->observer = observer;
#endif
    receiver->fd          = -1;
    receiver->initialized = 1;
#if STROOM_DIAGNOSTICS
    receiver->stage = "NO URL";
#endif
}

/**
 * @brief Release an unsuccessful attempt while retaining its URL and retry count.
 * @param receiver Fetch state.
 * @param now Failure timestamp.
 */
static void failed(ArtworkReceiver* receiver, uint32_t now)
{
#if STROOM_DIAGNOSTICS
    receiver->error = errno;

    if (!receiver->failure)
    {
        receiver->failure = "SOCKET ERROR";
    }
#endif

    if (receiver->fd >= 0)
    {
        close_socket(receiver);

        receiver->fd = -1;
    }

    release_data(receiver);

    receiver->blob.data = NULL;
    receiver->capacity  = 0;
    receiver->retry_at  = now;
}

/**
 * @brief Resolve SDK-collapsed errors without treating an empty read as failure.
 * @param receiver Fetch state with the socket.
 * @return Nonzero when the operation can be retried.
 */
static int retry(ArtworkReceiver* receiver)
{
    ARTWORK_TIMING_BEGIN(error_begin);

    int code = platform_socket_error(receiver->fd);

    ARTWORK_TIMING_END(receiver, DIAGNOSTIC_ARTWORK_SOCKET_ERROR, error_begin, code, 0, 0);

    return !code || code == EAGAIN || code == EWOULDBLOCK || code == EINTR || code == EINPROGRESS || code == EALREADY || code == ENOTCONN;
}

void artwork_receiver_step(ArtworkReceiver* receiver, const TrackMetadata* metadata, ArtworkIdentity identity, uint32_t now, int (*nonblocking)(int), unsigned read_limit)
{
    const TrackMetadata empty = { 0 };

    if (!metadata)
    {
        metadata = &empty;
    }

    const char* url = metadata->artwork_url;

    if (!receiver->initialized || !track_artwork_equal(metadata, &receiver->blob.metadata, identity))
    {
        artwork_receiver_close(receiver);

        receiver->metadata_at = now;
    }

    receiver->blob.metadata = *metadata;

    /* Settle source-specific identity changes before fetching. */

    if (!*url || receiver->complete || (receiver->fd < 0 && (uint32_t)(now - receiver->metadata_at) < ARTWORK_SETTLE_MS) ||
        (uint32_t)(now - receiver->polled) < ARTWORK_POLL_MS)
    {
        return;
    }

    receiver->polled = now;

    if (receiver->fd < 0)
    {
        uint32_t delay = receiver->attempts >= ARTWORK_ATTEMPTS ? ARTWORK_RETRY_SLOW_MS : ARTWORK_RETRY_MS;

        if (receiver->attempts && (uint32_t)(now - receiver->retry_at) < delay)
        {
            return;
        }

        ArtworkUrl parsed;

        if (artwork_url_parse(url, &parsed) != 0)
        {
#if STROOM_DIAGNOSTICS
            receiver->stage   = "URL";
            receiver->failure = "UNSUPPORTED URL";
#endif
            receiver->complete = 1;

            return;
        }

        ++receiver->attempts;
        memset(&receiver->http, 0, sizeof(receiver->http));

        receiver->sent         = 0;
        receiver->request_size = 0;
        receiver->started      = now;
        receiver->progress_at  = now;

#if STROOM_DIAGNOSTICS
        receiver->received = 0;
        receiver->failure  = NULL;
        receiver->stage    = "SOCKET";
        receiver->error    = 0;
#endif
        ARTWORK_TIMING_BEGIN(socket_begin);

        receiver->fd = socket(AF_INET, SOCK_STREAM, 0);

        ARTWORK_TIMING_END(receiver, DIAGNOSTIC_ARTWORK_SOCKET, socket_begin, (unsigned)receiver->fd, receiver->attempts, 0);

        int configured = receiver->fd >= 0;

        if (configured)
        {
            ARTWORK_TIMING_BEGIN(configure_begin);

            configured = nonblocking(receiver->fd) == 0;

            ARTWORK_TIMING_END(receiver, DIAGNOSTIC_ARTWORK_NONBLOCKING, configure_begin, configured, 0, 0);
        }

        if (!configured)
        {
            failed(receiver, now);
            return;
        }

        struct sockaddr_in address = { 0 };

        address.sin_family      = AF_INET;
        address.sin_port        = htons((uint16_t)parsed.port);
        address.sin_addr.s_addr = htonl(parsed.address);
        receiver->request_size  = (unsigned)snprintf(receiver->request, sizeof(receiver->request), "GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\nAccept: image/jpeg, image/png\r\nAccept-Encoding: identity\r\n\r\n", parsed.path, parsed.host, parsed.port);

#if STROOM_DIAGNOSTICS
        receiver->stage = "CONNECT";
#endif

        ARTWORK_TIMING_BEGIN(connect_begin);

        int connected = connect(receiver->fd, (struct sockaddr*)&address, sizeof(address));

        ARTWORK_TIMING_END(receiver, DIAGNOSTIC_ARTWORK_CONNECT, connect_begin, (unsigned)connected, parsed.address, parsed.port);

        if (connected < 0 && !retry(receiver))
        {
            failed(receiver, now);
        }

        return;
    }

    if ((uint32_t)(now - receiver->started) >= ARTWORK_TIMEOUT_MS ||
        (uint32_t)(now - receiver->progress_at) >= ARTWORK_STALL_MS)
    {
#if STROOM_DIAGNOSTICS
        receiver->failure = "TIMEOUT";
        errno             = ETIMEDOUT;
#endif
        failed(receiver, now);
        return;
    }

    if (receiver->sent < receiver->request_size)
    {
#if STROOM_DIAGNOSTICS
        receiver->stage = "SEND";
#endif
        ARTWORK_TIMING_BEGIN(send_begin);

        int n = send(receiver->fd, receiver->request + receiver->sent, receiver->request_size - receiver->sent, 0);

        ARTWORK_TIMING_END(receiver, DIAGNOSTIC_ARTWORK_SEND, send_begin, (unsigned)n, receiver->request_size - receiver->sent, receiver->sent);

        if (n > 0)
        {
            receiver->sent += (unsigned)n;
            receiver->progress_at = now;
        }
        else if (!n || !retry(receiver))
        {
            failed(receiver, now);
        }

        return;
    }

    uint8_t  data[ARTWORK_READ_BYTES] __attribute__((aligned(64)));
    uint32_t began = read_limit > 1 ? platform_millis() : 0;

    for (unsigned read = 0; read < read_limit; ++read)
    {
#if STROOM_DIAGNOSTICS
        receiver->stage = receiver->http.status_code ? "RECEIVE BODY" : "RECEIVE HEADERS";
#endif
        ARTWORK_TIMING_BEGIN(receive_begin);

        int n = recv(receiver->fd, data, sizeof(data), 0);

        ARTWORK_TIMING_END(receiver, DIAGNOSTIC_ARTWORK_RECEIVE, receive_begin, (unsigned)n, sizeof(data), receiver->http.used);

        if (n < 0)
        {
            if (!retry(receiver))
            {
                failed(receiver, now);
            }

            return;
        }

        if (n > 0)
        {
            receiver->progress_at = now;
#if STROOM_DIAGNOSTICS
            receiver->received += (unsigned)n;
#endif
            unsigned needed = receiver->http.used + (unsigned)n;

            if (needed > ARTWORK_MAX_BYTES)
            {
                needed = ARTWORK_MAX_BYTES;
            }

            if (needed > receiver->capacity)
            {
                unsigned capacity = receiver->capacity ? receiver->capacity : ARTWORK_BUFFER_INITIAL;

                while (capacity < needed)
                {
                    capacity *= 2;
                }

                ARTWORK_TIMING_BEGIN(grow_begin);

                uint8_t* grown = realloc(receiver->blob.data, capacity);

                ARTWORK_TIMING_END(receiver, DIAGNOSTIC_ARTWORK_GROW, grow_begin, receiver->capacity, capacity, grown != NULL);

                if (!grown)
                {
#if STROOM_DIAGNOSTICS
                    receiver->failure = "ARTWORK OUT OF MEMORY";
                    errno             = ENOMEM;
#endif
                    failed(receiver, now);
                    return;
                }

                receiver->blob.data = grown;
                receiver->capacity  = capacity;
            }
        }

        ARTWORK_TIMING_BEGIN(parse_begin);

        int accepted = n ? artwork_http_feed(&receiver->http, data, (size_t)n, receiver->blob.data) : artwork_http_eof(&receiver->http);

        ARTWORK_TIMING_END(receiver, DIAGNOSTIC_ARTWORK_PARSE, parse_begin, (unsigned)n, receiver->http.used, accepted);

        if (accepted < 0)
        {
#if STROOM_DIAGNOSTICS
            receiver->failure = receiver->http.too_large ? "ARTWORK EXCEEDS 2 MIB" : n ? "HTTP REJECTED"
                                                                                       : "TRUNCATED HTTP";
            errno             = 0;
#endif
            failed(receiver, now);

            receiver->complete = receiver->http.too_large;

            return;
        }

#if STROOM_DIAGNOSTICS
        receiver->stage = receiver->http.status_code ? "RECEIVE BODY" : "RECEIVE HEADERS";
#endif

        if (receiver->http.done)
        {
#if STROOM_DIAGNOSTICS
            receiver->stage = "DOWNLOADED";
#endif
            receiver->blob.size = receiver->http.used;
            receiver->complete  = 1;

            close_socket(receiver);

            receiver->fd = -1;

            return;
        }

        /* Do not probe again after a short read, or extend a slow RPC burst. */

        if ((unsigned)n < sizeof(data) ||
            (read + 1 < read_limit && (uint32_t)(platform_millis() - began) >= ARTWORK_BURST_MS))
        {
            return;
        }
    }
}

int artwork_receiver_take(ArtworkReceiver* receiver, ArtworkBlob* result)
{
    if (!receiver->complete || !receiver->blob.data)
    {
        return 1;
    }

    *result             = receiver->blob;
    receiver->blob.data = NULL;
    receiver->capacity  = 0;

    return 0;
}
