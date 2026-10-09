#include "audio/network/ariacast/client.h"
#include "audio/network/ariacast/server.h"
#include "audio/network/ariacast/socket.h"
#include "platform/time/clock.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <strings.h>

#define ASCII_DELETE          127
#define DECIMAL_BASE          10
#define JSON_REPLY_BYTES      2048
#define HEARTBEAT_INTERVAL_MS 10000
#define HEARTBEAT_TIMEOUT_MS  5000
#define HTTP_TIMEOUT_MS       3000
#define STATS_INTERVAL_MS     1000
#define SOCKET_READ_BYTES     4096

/** @brief Release transport, optionally preserving complete PCM for playback. */
static void close_client(AriaServer* server, AriaClient* c, const char* reason, int code, int drain, uint32_t now)
{
    (void)reason;
    (void)code;

    if (c->fd < 0)
    {
        return;
    }

    if (server->audio_fd == c->fd)
    {
#if STROOM_DIAGNOSTICS
        ++server->diagnostics.disconnects;
        snprintf(server->diagnostics.last_disconnect, sizeof(server->diagnostics.last_disconnect), "%s", reason);

        server->diagnostics.last_disconnect_error  = code;
        server->diagnostics.last_disconnect_queued = server->stream.count;
        server->diagnostics.last_disconnect_gap    = server->stream.has_pcm ? (uint32_t)((drain ? now : server->last_poll) - server->stream.last_pcm) : 0;
#endif
        server->audio_fd         = -1;
        server->stream.connected = 0;
        server->stream.ending    = drain && server->stream.has_pcm;
        server->stream.finished  = 0;
        server->stream.ending_at = now;

        /* Metadata belongs to the connection, not its buffered audio tail. */
        memset(&server->metadata, 0, sizeof(server->metadata));

        server->metadata_peer = 0;

        ++server->metadata_revision;

        if (!server->stream.ending)
        {
            server->stream.listening      = 0;
            server->stream.device_name[0] = 0;
            server->stream.count = server->stream.read = 0;

            ++server->stream.generation;
        }
    }

    close(c->fd);
    memset(c, 0, sizeof(*c));

    c->fd = -1;
}

void aria_client_close(AriaServer* server, AriaClient* c, const char* reason, int code)
{
    close_client(server, c, reason, code, 0, 0);
}

void aria_client_end(AriaServer* server, AriaClient* c, const char* reason, int code, uint32_t now)
{
    close_client(server, c, reason, code, 1, now);
}

/**
 * @brief Append response bytes, rejecting backlogged peers.
 * @param c Client.
 * @param data Bytes.
 * @param size Length.
 * @return Nonzero on success.
 */
static int append(AriaClient* c, const void* data, unsigned size)
{
    if (size > sizeof(c->output) - c->output_used)
    {
        return 0;
    }

    memcpy(c->output + c->output_used, data, size);

    c->output_used += size;

    return 1;
}

/**
 * @brief Queue a small server WebSocket message.
 * @param c Client.
 * @param opcode Type.
 * @param data Payload.
 * @param size Length.
 * @return Nonzero on success.
 */
static int frame(AriaClient* c, unsigned opcode, const void* data, unsigned size)
{
    unsigned n = aria_websocket_frame(c->output + c->output_used, sizeof(c->output) - c->output_used, opcode, data, size);

    c->output_used += n;

    return n != 0;
}

/**
 * @brief Queue a text frame.
 * @param c Client.
 * @param text JSON.
 * @return Nonzero on success.
 */
static int text(AriaClient* c, const char* text)
{
    return frame(c, 1, text, (unsigned)strlen(text));
}

/**
 * @brief Queue current labels for a metadata subscriber.
 * @param server Receiver state.
 * @param c Subscriber.
 * @return Nonzero when queued successfully.
 */
static int metadata_reply(const AriaServer* server, AriaClient* c)
{
    char json[TRACK_METADATA_JSON_BYTES];

    track_metadata_to_json(&server->metadata, json);

    c->metadata_revision = server->metadata_revision;

    return text(c, json);
}

/**
 * @brief Validate metadata and restrict updates to the current sender's address.
 * @param server Receiver state.
 * @param c Requesting client.
 * @param data JSON bytes.
 * @param size JSON byte count.
 * @param now Monotonic milliseconds.
 * @return Parsed action, or INVALID for rejected requests.
 */
static TrackMetadataAction metadata_apply(AriaServer* server, const AriaClient* c, const void* data, unsigned size, uint32_t now)
{
    TrackMetadata       pending = server->metadata;
    TrackMetadataAction action  = track_metadata_apply_json(&pending, data, size);

    if (action == TRACK_METADATA_INVALID || action == TRACK_METADATA_GET)
    {
        return action;
    }

    if (!server->permitted)
    {
        return TRACK_METADATA_INVALID;
    }

    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        const AriaClient* sender = &server->clients[i];

        if (server->audio_fd >= 0 && sender->fd == server->audio_fd && sender->peer_address != c->peer_address)
        {
            return TRACK_METADATA_INVALID;
        }
    }

    if (server->metadata_peer != c->peer_address ||
        (server->audio_fd < 0 && (uint32_t)(now - server->metadata_at) >= ARIA_FIRST_PCM_TIMEOUT_MS))
    {
        memset(&pending, 0, sizeof(pending));
        track_metadata_apply_json(&pending, data, size);
    }

    server->metadata      = pending;
    server->metadata_peer = c->peer_address;
    server->metadata_at   = now;

    ++server->metadata_revision;

    return action;
}

/**
 * @brief Apply an update and retain its exact input for UDP inspection.
 * @param server Receiver state.
 * @param c Requesting client.
 * @param data JSON bytes.
 * @param size JSON byte count.
 * @param now Monotonic milliseconds.
 * @return Parsed action, or INVALID for rejected requests.
 */
static TrackMetadataAction metadata_update(AriaServer* server, const AriaClient* c, const void* data, unsigned size, uint32_t now)
{
    TrackMetadataAction action = metadata_apply(server, c, data, size, now);

#if STROOM_DIAGNOSTICS
    if (action != TRACK_METADATA_GET && size <= ARIA_MESSAGE_BYTES)
    {
        server->diagnostics.metadata_request[0] = (uint8_t)action;

        memcpy(server->diagnostics.metadata_request + 1, data, size);

        server->diagnostics.metadata_request_size = size + 1;
    }
#endif

    return action;
}

/**
 * @brief Queue an HTTP response and close afterward.
 * @param c Client.
 * @param status Status phrase.
 * @param body Response body.
 * @return Nonzero on success.
 */
static int response(AriaClient* c, const char* status, const char* body)
{
    char header[256];
    int  n = snprintf(header, sizeof(header), "HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", status, (unsigned)strlen(body));

    c->closing = 1;

    return append(c, header, (unsigned)n) && append(c, body, (unsigned)strlen(body));
}

/**
 * @brief Test for a comma-separated HTTP token.
 * @param value Header value.
 * @param token Expected token.
 * @return Nonzero if present.
 */
static int token(const char* value, const char* token)
{
    size_t length = strlen(token);

    while (*value)
    {
        while (*value == ' ' || *value == '\t' || *value == ',')
        {
            ++value;
        }

        const char* end = value;

        while (*end && *end != ',' && *end != ' ' && *end != '\t')
        {
            ++end;
        }

        if ((size_t)(end - value) == length && !strncasecmp(value, token, length))
        {
            return 1;
        }

        value = end;
    }

    return 0;
}

/**
 * @brief Parse a complete HTTP header and establish an endpoint.
 * @param server Receiver.
 * @param c Client.
 * @param now Monotonic receipt time in milliseconds.
 * @return Nonzero if accepted or a response was queued.
 */
static int request(AriaServer* server, AriaClient* c, uint32_t now)
{
    char  method[8], path[64], version[16], extra;
    char* line_end = strstr(c->header, "\r\n");

    if (!line_end)
    {
        return 0;
    }

    *line_end = 0;

    if (sscanf(c->header, "%7s %63s %15s %c", method, path, version, &extra) != 3 || strcmp(version, "HTTP/1.1") != 0)
    {
        return response(c, "400 Bad Request", "{}");
    }

    char     key[32] = { 0 };
    unsigned body    = 0;
    int      upgrade = 0, connection = 0, ws_version = 0, have_length = 0;
    char*    line = line_end + 2;

    while (*line && *line != '\r')
    {
        char* end = strstr(line, "\r\n");

        if (!end)
        {
            return 0;
        }

        *end        = 0;
        char* colon = strchr(line, ':');

        if (!colon)
        {
            return response(c, "400 Bad Request", "{}");
        }

        *colon++ = 0;

        while (*colon == ' ' || *colon == '\t')
        {
            ++colon;
        }

        char* tail = colon + strlen(colon);

        while (tail > colon && (tail[-1] == ' ' || tail[-1] == '\t'))
        {
            *--tail = 0;
        }

        if (!strcasecmp(line, "Upgrade"))
        {
            upgrade = !strcasecmp(colon, "websocket");
        }
        else if (!strcasecmp(line, "Connection"))
        {
            connection = token(colon, "upgrade");
        }
        else if (!strcasecmp(line, "Sec-WebSocket-Version"))
        {
            ws_version = !strcmp(colon, "13");
        }
        else if (!strcasecmp(line, "Sec-WebSocket-Key"))
        {
            if (key[0] || strlen(colon) >= sizeof(key))
            {
                return response(c, "400 Bad Request", "{}");
            }

            memcpy(key, colon, strlen(colon) + 1);
        }
        else if (!strcasecmp(line, "Transfer-Encoding"))
        {
            return response(c, "400 Bad Request", "{}");
        }
        else if (!strcasecmp(line, "Content-Length"))
        {
            if (have_length++ || !*colon)
            {
                return response(c, "400 Bad Request", "{}");
            }

            for (char* p = colon; *p; ++p)
            {
                if (*p < '0' || *p > '9' || body > ARIA_MESSAGE_BYTES / DECIMAL_BASE)
                {
                    return response(c, "413 Content Too Large", "{}");
                }

                body = body * DECIMAL_BASE + (unsigned)(*p - '0');
            }

            if (body > ARIA_MESSAGE_BYTES)
            {
                return response(c, "413 Content Too Large", "{}");
            }
        }

        line = end + 2;
    }

    if (!strcmp(method, "POST") && !strcmp(path, "/metadata"))
    {
        c->body_left = body;

        return body ? 1 : response(c, "400 Bad Request", "{\"success\":false}");
    }

    if (strcmp(method, "GET") != 0 || !upgrade || !connection || !ws_version || body)
    {
        return response(c, "400 Bad Request", "{}");
    }

    int endpoint = !strcmp(path, "/audio") ? ARIA_ENDPOINT_AUDIO : !strcmp(path, "/control") ? ARIA_ENDPOINT_CONTROL
                                                               : !strcmp(path, "/stats")     ? ARIA_ENDPOINT_STATS
                                                               : !strcmp(path, "/metadata")  ? ARIA_ENDPOINT_METADATA
                                                                                             : 0;

    if (!endpoint)
    {
        return response(c, "404 Not Found", "{}");
    }

    if (endpoint == ARIA_ENDPOINT_AUDIO && (!server->permitted || server->audio_fd >= 0))
    {
        return response(c, "403 Forbidden", "{}");
    }

    char accept[ARIA_WS_ACCEPT_BYTES];

    if (aria_websocket_accept(key, accept) != 0)
    {
        return response(c, "400 Bad Request", "{}");
    }

    char header[256];
    int  n = snprintf(header, sizeof(header), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);

    if (!append(c, header, (unsigned)n))
    {
        return 0;
    }

    c->endpoint = endpoint;

    if (endpoint == ARIA_ENDPOINT_AUDIO)
    {
        if (server->metadata_peer != c->peer_address || (uint32_t)(now - server->metadata_at) >= ARIA_FIRST_PCM_TIMEOUT_MS)
        {
            memset(&server->metadata, 0, sizeof(server->metadata));
            ++server->metadata_revision;
        }

        server->metadata_peer = c->peer_address;
        c->audio_opened       = now;
#if STROOM_DIAGNOSTICS
        server->diagnostics.audio_bytes = 0;
        server->diagnostics.audio_reads = 0;

        aria_diagnostics_session(&server->diagnostics);
#endif
        server->audio_fd              = c->fd;
        server->stream.ending         = 0;
        server->stream.finished       = 0;
        server->stream.connected      = 1;
        server->stream.listening      = 0;
        server->stream.device_name[0] = 0;
        server->stream.has_pcm        = 0;
        server->stream.received = server->stream.read = server->stream.count = 0;

        ++server->stream.generation;

        char ready[256];

        snprintf(ready, sizeof(ready), "{\"status\":\"READY\",\"sample_rate\":%u,\"channels\":%u,\"frame_size\":%u,\"stroom_listen\":true,\"stroom_device_name_bytes\":%u}", (unsigned)AUDIO_RATE, (unsigned)ARIA_PCM_CHANNELS, (unsigned)ARIA_PCM_BYTES, (unsigned)METADATA_DEVICE_NAME_BYTES - 1);
        return text(c, ready);
    }

    if (endpoint == ARIA_ENDPOINT_CONTROL)
    {
        return text(c, "{\"status\":\"READY\",\"volume_available\":false,\"current_volume\":-1}");
    }

    if (endpoint == ARIA_ENDPOINT_METADATA)
    {
        return metadata_reply(server, c);
    }

    return 1;
}

/**
 * @brief Context for one decoded WebSocket message.
 */
typedef struct
{
    AriaServer* server; /**< Receiver. */
    AriaClient* client; /**< Origin connection. */
    uint32_t    now;    /**< Current receipt time. */
} MessageContext;

/**
 * @brief Process decoded PCM and WebSocket control frames.
 * @param context Connection context.
 * @param opcode Frame type.
 * @param data Payload.
 * @param size Bytes.
 * @return Nonzero to retain the connection.
 */
static int message(void* context, unsigned opcode, const uint8_t* data, unsigned size)
{
    MessageContext* m      = context;
    AriaServer*     server = m->server;
    AriaClient*     c      = m->client;

    if (opcode == 8)
    {
        c->closing = 1;

        return frame(c, 8, data, size);
    }

    if (opcode == ARIA_WS_PING)
    {
        return frame(c, ARIA_WS_PONG, data, size);
    }

    if (opcode == ARIA_WS_PONG)
    {
        if (c->ping_token && size == sizeof(c->ping_token) && !memcmp(data, &c->ping_token, size))
        {
            c->ping_token   = 0;
            c->heartbeat_at = m->now;
#if STROOM_DIAGNOSTICS
            ++server->diagnostics.pongs;

            server->diagnostics.last_pong_address = c->peer_address;
#endif
        }

        return 1;
    }

    if (c->endpoint == ARIA_ENDPOINT_AUDIO && opcode == ARIA_WS_TEXT &&
        size >= sizeof(ARIA_LISTEN_REQUEST) - 1 && !memcmp(data, ARIA_LISTEN_REQUEST, sizeof(ARIA_LISTEN_REQUEST) - 1))
    {
        /* Fix output policy before the first PCM. Never acknowledge silence
         * after audio might already have reached the sound device. */

        if (server->stream.has_pcm)
        {
            return 0;
        }

        unsigned prefix    = sizeof(ARIA_LISTEN_REQUEST) - 1;
        unsigned name_size = size > prefix ? size - prefix - 1 : 0;

        if (size > prefix && (data[prefix] != ' ' || !name_size || name_size >= METADATA_DEVICE_NAME_BYTES))
        {
            return 0;
        }

        for (unsigned i = 0; i < name_size; ++i)
        {
            if (data[prefix + 1 + i] < ' ' || data[prefix + 1 + i] == ASCII_DELETE)
            {
                return 0;
            }
        }

        if (name_size)
        {
            memcpy(server->stream.device_name, data + prefix + 1, name_size);
        }

        server->stream.device_name[name_size] = 0;
        server->stream.listening              = 1;

        return text(c, ARIA_LISTEN_ACK);
    }

    if (c->endpoint == ARIA_ENDPOINT_AUDIO && opcode == 2)
    {
#if STROOM_DIAGNOSTICS
        if (server->stream.has_pcm && (uint32_t)(m->now - server->stream.last_pcm) > server->diagnostics.max_pcm_gap)
        {
            server->diagnostics.max_pcm_gap = m->now - server->stream.last_pcm;
        }
#endif
        return aria_stream_push(&server->stream, data, size, m->now) == 0;
    }

    if (c->endpoint == ARIA_ENDPOINT_METADATA && opcode == ARIA_WS_TEXT)
    {
        TrackMetadataAction action = metadata_update(server, c, data, size, m->now);

        if (action == TRACK_METADATA_GET)
        {
            return metadata_reply(server, c);
        }

        return text(c, action == TRACK_METADATA_INVALID ? "{\"type\":\"error\",\"message\":\"Invalid metadata\"}" : "{\"type\":\"ack\",\"success\":true}");
    }

    if (c->endpoint == ARIA_ENDPOINT_CONTROL && opcode == 1)
    {
        return text(c, "{\"success\":false,\"volume_available\":false,\"level\":-1}");
    }

    return 1;
}

/**
 * @brief Feed received HTTP or WebSocket bytes to a client session.
 * @param server Receiver.
 * @param c Connection.
 * @param data Received bytes.
 * @param size Byte count.
 * @param now Monotonic milliseconds.
 * @return Nonzero while the request is valid.
 */
static int receive_bytes(AriaServer* server, AriaClient* c, const uint8_t* data, unsigned size, uint32_t now)
{
    while (size && !c->closing)
    {
        if (c->endpoint)
        {
            MessageContext context = { server, c, now };

            return aria_websocket_feed(&c->websocket, data, size, message, &context) == 0;
        }

        if (c->body_left)
        {
            unsigned used = size < c->body_left ? size : c->body_left;

            memcpy(c->websocket.data + c->body_used, data, used);

            c->body_used += used;
            c->body_left -= used;
            size -= used;
            data += used;

            if (!c->body_left)
            {
                TrackMetadataAction action = metadata_update(server, c, c->websocket.data, c->body_used, now);

                return response(c, action == TRACK_METADATA_INVALID ? "400 Bad Request" : "200 OK", action == TRACK_METADATA_INVALID ? "{\"success\":false}" : "{\"success\":true}");
            }

            continue;
        }

        if (c->header_used + 1 >= sizeof(c->header) || !*data)
        {
            return 0;
        }

        c->header[c->header_used++] = (char)*data++;

        --size;

        c->header[c->header_used] = 0;

        if (c->header_used >= 4 && !memcmp(c->header + c->header_used - 4, "\r\n\r\n", 4))
        {
            if (!request(server, c, now))
            {
                return 0;
            }
        }
    }

    return 1;
}

void aria_clients_tick(AriaServer* server, uint32_t now)
{
    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        AriaClient* c = &server->clients[i];

        if (c->fd < 0)
        {
            continue;
        }

        if (c->endpoint == ARIA_ENDPOINT_METADATA && !c->closing && !c->output_used && c->metadata_revision != server->metadata_revision)
        {
            metadata_reply(server, c);
        }

        if (c->endpoint == ARIA_ENDPOINT_AUDIO && !server->stream.has_pcm &&
            (uint32_t)(now - c->audio_opened) >= ARIA_FIRST_PCM_TIMEOUT_MS)
        {
            aria_client_close(server, c, "first PCM timeout", 0);
            continue;
        }

        if ((uint32_t)(now - c->touched) >= HTTP_TIMEOUT_MS && (!c->endpoint || c->closing))
        {
            aria_client_close(server, c, "protocol or response failure", 0);
            continue;
        }

        if (c->endpoint && !c->closing && c->endpoint != ARIA_ENDPOINT_AUDIO)
        {
            if (c->ping_token && (uint32_t)(now - c->heartbeat_at) >= HEARTBEAT_TIMEOUT_MS)
            {
#if STROOM_DIAGNOSTICS
                ++server->diagnostics.heartbeat_timeouts;
#endif
                aria_client_close(server, c, "protocol or response failure", 0);
                continue;
            }

            if (!c->ping_token && (uint32_t)(now - c->heartbeat_at) >= HEARTBEAT_INTERVAL_MS)
            {
                if (!++server->heartbeat_serial)
                {
                    ++server->heartbeat_serial;
                }

                c->ping_token   = server->heartbeat_serial;
                c->heartbeat_at = now;

                if (!frame(c, ARIA_WS_PING, &c->ping_token, sizeof(c->ping_token)))
                {
                    aria_client_close(server, c, "protocol or response failure", 0);
                    continue;
                }

#if STROOM_DIAGNOSTICS
                ++server->diagnostics.pings;
#endif
            }
        }

        if (c->endpoint == ARIA_ENDPOINT_STATS && !c->closing && (uint32_t)(now - c->stats_at) >= STATS_INTERVAL_MS)
        {
            char stats[JSON_REPLY_BYTES];

            aria_statistics(server, stats, sizeof(stats));

            if (!text(c, stats))
            {
                aria_client_close(server, c, "protocol or response failure", 0);
                continue;
            }

            c->stats_at = now;
        }
    }
}

void aria_clients_service(AriaServer* server, uint32_t now)
{
    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        AriaClient* c = &server->clients[i];

        if (c->fd < 0)
        {
            continue;
        }

        if (c->output_used)
        {
            server->operation = "SEND";

            int n = send(c->fd, c->output + c->output_sent, c->output_used - c->output_sent, 0);

            if (n < 0 && aria_socket_retry(c->fd))
            {
                continue;
            }

            if (n <= 0)
            {
                aria_client_end(server, c, "socket send failure", n < 0 ? errno : 0, now);
                continue;
            }

            c->output_sent += (unsigned)n;

            if (c->output_sent == c->output_used)
            {
                c->output_used = c->output_sent = 0;
            }
        }

        if (c->closing && !c->output_used)
        {
            aria_client_end(server, c, "peer sent WebSocket Close", 0, now);
            continue;
        }

        if (!c->closing)
        {
            uint8_t  data[SOCKET_READ_BYTES];
            unsigned read_bytes = sizeof(data);

            if (c->endpoint == ARIA_ENDPOINT_AUDIO)
            {
                unsigned free_messages = ARIA_QUEUE_MESSAGES - server->stream.count;

                if (!free_messages)
                {
#if STROOM_DIAGNOSTICS
                    ++server->diagnostics.paused_reads;
#endif
                    continue;
                }

                unsigned pending = c->websocket.message_opcode == 2 ? c->websocket.used : 0;

                if (pending >= ARIA_PCM_BYTES)
                {
                    pending = ARIA_PCM_BYTES - 1;
                }

                unsigned capacity = free_messages * ARIA_PCM_BYTES - pending;

                if (read_bytes > capacity)
                {
                    read_bytes = capacity;
                }

#if STROOM_DIAGNOSTICS
                ++server->diagnostics.audio_reads;
#endif
            }

            server->operation = "RECEIVE";
#if STROOM_DIAGNOSTICS
            uint32_t read_at = platform_millis();
#endif
            int n          = recv(c->fd, data, read_bytes, 0);
            int read_error = n < 0 ? errno : 0;
#if STROOM_DIAGNOSTICS
            uint32_t read_done = platform_millis();

            if (c->endpoint == ARIA_ENDPOINT_AUDIO)
            {
                AriaDiagnosticCaptureRecord record = { .at = read_done, .kind = DIAGNOSTIC_CAPTURE_READ, .data = { [DIAGNOSTIC_CAPTURE_READ_RESULT] = (uint32_t)n, [DIAGNOSTIC_CAPTURE_READ_ERRNO] = (uint32_t)read_error, [DIAGNOSTIC_CAPTURE_READ_DURATION_MS] = read_done - read_at, [DIAGNOSTIC_CAPTURE_READ_REQUESTED_BYTES] = read_bytes, [DIAGNOSTIC_CAPTURE_READ_SOCKET_BYTES] = server->diagnostics.audio_bytes + (n > 0 ? (unsigned)n : 0), [DIAGNOSTIC_CAPTURE_READ_SESSION] = server->stream.generation } };

                aria_diagnostic_capture_record(&server->diagnostics.diagnostic_capture, record);
            }
#endif
            errno = read_error;

            if (n < 0 && aria_socket_retry(c->fd))
            {
                if (c->endpoint == ARIA_ENDPOINT_AUDIO)
                {
#if STROOM_DIAGNOSTICS
                    ++server->diagnostics.empty_reads;
#endif
                }

                continue;
            }

#if STROOM_DIAGNOSTICS
            if (n > 0 && c->endpoint == ARIA_ENDPOINT_AUDIO)
            {
                AriaDiagnostics* d = &server->diagnostics;

                if (d->last_bytes_at && (uint32_t)(now - d->last_bytes_at) > d->max_byte_gap)
                {
                    d->max_byte_gap = now - d->last_bytes_at;
                }

                d->last_bytes_at = now;
                server->diagnostics.audio_bytes += (unsigned)n;
            }
#endif

            if (n <= 0 || !receive_bytes(server, c, data, (unsigned)n, now))
            {
                const char* reason = n < 0 ? "socket receive error" : n == 0 ? "peer closed TCP"
                                                                             : "invalid WebSocket or PCM";

                if (n <= 0)
                {
                    aria_client_end(server, c, reason, n < 0 ? errno : 0, now);
                }
                else
                {
                    aria_client_close(server, c, reason, 0);
                }

                continue;
            }

            c->touched = now;
        }
    }
}
