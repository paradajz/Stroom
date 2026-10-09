#include "audio/network/cd_stream/server.h"
#include "contracts/audio.h"
#include "platform/network/socket_error.h"
#include "platform/network/socket_options.h"
#include "platform/network/tcp_state.h"
#include "util/diagnostics.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define HANDSHAKE_TIMEOUT_MS 5000u
#define SEND_TIMEOUT_MS      1500u
#define SEND_BUDGET          4
#define DISCOVERY_BYTES      1024
#define STALL_SAMPLE_MS      250u

static int retry(int fd)
{
    int error = platform_socket_error(fd);

    /* The PS2 bridge can collapse would-block to ENFILE with SO_ERROR == 0. */
    return error == 0 || error == EAGAIN || error == EWOULDBLOCK || error == EINTR || error == ENOMEM || error == ENOBUFS;
}

static void close_client(CdStreamClient* client)
{
    if (client->fd >= 0)
    {
        close(client->fd);
    }

    memset(client, 0, sizeof(*client));

    client->fd = -1;
}

static void drop_client(CdStreamServer* server, CdStreamClient* client, const char* reason, int error)
{
    ++server->disconnects;

    server->last_disconnect = reason;
    server->last_error      = error;

    STROOM_LOG("CD stream disconnect: %s errno=%d lag=%u partial=%u/%u", reason, error, (unsigned)(server->write - client->next), client->pending_sent, client->pending_size);
    close_client(client);
}

static void socket_failure(int fd, int type, const char* operation)
{
    int code = fd < 0 ? errno : platform_socket_error(fd);

    STROOM_LOG("CD stream %s %s failed: errno %d", type == SOCK_STREAM ? "TCP" : "UDP", operation, code);
    (void)code;
    (void)type;
    (void)operation;
}

static int bind_socket(CdStreamServer* server, int type)
{
    int fd = socket(AF_INET, type, 0);

    if (fd < 0)
    {
        socket_failure(fd, type, "SOCKET");
        return -1;
    }

    int                reuse = 1;
    struct sockaddr_in local = { 0 };
    local.sin_family         = AF_INET;
    local.sin_port           = htons(CD_STREAM_PORT);
    local.sin_addr.s_addr    = INADDR_ANY;

    /* The patched SDK enables reuse so disc changes can reopen this port
     * while old connections finish closing. Older resident stacks may reject it. */
    (void)setsockopt(fd, PLATFORM_SOCKET_LEVEL, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (server->nonblocking(fd) != 0)
    {
        socket_failure(fd, type, "NONBLOCK");
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr*)&local, sizeof(local)) < 0)
    {
        socket_failure(fd, type, "BIND");
        close(fd);
        return -1;
    }

    return fd;
}

int cd_stream_server_open(CdStreamServer* server, int (*nonblocking)(int))
{
    memset(server, 0, sizeof(*server));

    server->discovery       = -1;
    server->listener        = -1;
    server->nonblocking     = nonblocking;
    server->last_disconnect = "none";
    server->opening         = 1;

    for (unsigned i = 0; i < CD_STREAM_CLIENTS; ++i)
    {
        server->clients[i].fd = -1;
    }

    server->listener = bind_socket(server, SOCK_STREAM);

    if (server->listener < 0)
    {
        return CD_STREAM_SERVER_ERROR_OPEN;
    }

    if (listen(server->listener, CD_STREAM_CLIENTS) < 0)
    {
        socket_failure(server->listener, SOCK_STREAM, "LISTEN");
        return CD_STREAM_SERVER_ERROR_OPEN;
    }

    server->discovery = bind_socket(server, SOCK_DGRAM);

    return server->discovery < 0 ? CD_STREAM_SERVER_ERROR_OPEN : 0;
}

static void word(uint8_t* output, uint32_t value)
{
    value = htonl(value);

    memcpy(output, &value, sizeof(value));
}

static void record(CdStreamClient* client, unsigned type, const uint8_t* payload, unsigned bytes)
{
    memcpy(client->pending, CD_STREAM_MAGIC, 4);
    word(client->pending + 4, type);
    word(client->pending + 8, bytes);

    if (bytes)
    {
        memcpy(client->pending + CD_STREAM_HEADER_BYTES, payload, bytes);
    }

    client->pending_size = CD_STREAM_HEADER_BYTES + bytes;
    client->pending_sent = 0;
}

static void handshake(CdStreamServer* server, CdStreamClient* client, uint32_t now)
{
    const char expected[] = CD_STREAM_REQUEST;
    int        n          = recv(client->fd, client->request + client->request_used, sizeof(expected) - 1 - client->request_used, 0);

    if (n <= 0)
    {
        if (n == 0 || !retry(client->fd))
        {
            close_client(client);
        }

        return;
    }

    client->request_used += (unsigned)n;

    if (memcmp(client->request, expected, client->request_used) != 0)
    {
        close_client(client);
        return;
    }

    if (client->request_used == sizeof(expected) - 1)
    {
        uint8_t format[CD_STREAM_FORMAT_BYTES];

        word(format, AUDIO_RATE);
        word(format + 4, CD_STREAM_CHANNELS);
        word(format + 8, CD_STREAM_BITS);
        record(client, CD_STREAM_FORMAT, format, sizeof(format));

        /* Discovery/reconnection can finish just after playback starts. Reuse
         * the intact opening in the existing ring; later joins stay live. */
        client->next       = server->opening ? 0 : server->write;
        client->progress   = now;
        client->subscribed = 1;
        Ps2TcpState state;
        server->tcp_available = platform_tcp_state(client->fd, &state);
    }
}

static void send_audio(CdStreamServer* server, CdStreamClient* client, uint32_t now)
{
    unsigned lag = server->write - client->next;

    if (lag > server->max_lag_blocks)
    {
        server->max_lag_blocks = lag;
    }

    if (lag > CD_STREAM_BLOCKS)
    {
        drop_client(server, client, "queue_overrun", 0);
        return;
    }

    for (unsigned budget = 0; budget < SEND_BUDGET; ++budget)
    {
        if (client->pending_sent == client->pending_size)
        {
            client->pending_size = 0;
            client->pending_sent = 0;

            if (client->ending)
            {
                close_client(client);
                return;
            }

            if (client->next != server->write)
            {
                record(client, CD_STREAM_PCM, server->blocks[client->next % CD_STREAM_BLOCKS], CD_STREAM_MAX_PAYLOAD_BYTES);
                ++client->next;

                client->has_pcm = 1;

                client->progress = now;
            }
            else if (server->finished && client->has_pcm)
            {
                record(client, CD_STREAM_END, NULL, 0);

                client->ending   = 1;
                client->progress = now;
            }
            else
            {
                return;
            }
        }

        int n = send(client->fd, client->pending + client->pending_sent, client->pending_size - client->pending_sent, 0);

        if (n > 0)
        {
            if (client->retrying && client->stall_id == server->stall_id && client->stall_id)
            {
                server->stall_ms = now - client->retry_since;
            }

            client->sampled_at = 0;
            client->stall_id   = 0;
            client->retrying   = 0;
            client->pending_sent += (unsigned)n;
            client->progress = now;
        }
        else
        {
            int error = n == 0 ? EAGAIN : platform_socket_error(client->fd);

            if (error != 0 && error != EAGAIN && error != EWOULDBLOCK && error != EINTR && error != ENOMEM && error != ENOBUFS)
            {
                drop_client(server, client, "send_error", error);
                return;
            }

            ++server->send_retries;

            if (!client->retrying)
            {
                client->retrying    = 1;
                client->retry_since = now;
            }

            unsigned waiting = now - client->retry_since;

            if (waiting >= STALL_SAMPLE_MS && (!client->sampled_at || waiting - client->sampled_at >= STALL_SAMPLE_MS))
            {
                if (!client->stall_id)
                {
                    client->stall_id = ++server->stall_id;
                }

                if (client->stall_id == server->stall_id)
                {
                    server->stall_sample_ms = waiting;
                    server->stall_ms        = waiting;
                    server->stall_lag       = server->write - client->next;
                    server->tcp_available   = platform_tcp_state(client->fd, &server->stall_tcp);

                    platform_net_tx_stats(&server->stall_driver);
                }

                client->sampled_at = waiting;
            }

            if ((uint32_t)(now - client->progress) >= SEND_TIMEOUT_MS)
            {
                drop_client(server, client, "send_stall", error);
            }

            return;
        }
    }
}

int cd_stream_server_step(CdStreamServer* server, uint32_t now, const char* address)
{
    if (server->last_poll && (uint32_t)(now - server->last_poll) > server->max_poll_gap_ms)
    {
        server->max_poll_gap_ms = now - server->last_poll;
    }

    server->last_poll = now;

    char               query[CD_STREAM_REQUEST_BYTES];
    struct sockaddr_in peer   = { 0 };
    socklen_t          length = sizeof(peer);
    int                n      = recvfrom(server->discovery, query, sizeof(query), 0, (struct sockaddr*)&peer, &length);

    if (n < 0 && !retry(server->discovery))
    {
        return CD_STREAM_SERVER_ERROR_SOCKET;
    }

    if (n == (int)strlen(CD_STREAM_DISCOVER) && !memcmp(query, CD_STREAM_DISCOVER, (unsigned)n) &&
        address[0] && length == sizeof(peer) && peer.sin_family == AF_INET && peer.sin_port && peer.sin_addr.s_addr)
    {
        char reply[DISCOVERY_BYTES];
        int  bytes = snprintf(reply, sizeof(reply), "{\"service\":\"%s\",\"version\":%u,\"name\":\"Stroom CD\",\"port\":%u,\"sample_rate\":%u,\"channels\":%u,\"bits\":%u,\"stats\":{\"disconnects\":%u,\"send_retries\":%u,\"resets\":%u,\"mailbox_overflows\":%u,\"max_lag_blocks\":%u,\"max_poll_gap_ms\":%u,\"last_disconnect\":\"%s\",\"last_error\":%d,\"source\":{\"blocks\":%u,\"gaps\":%u,\"max_gap_ms\":%u,\"drive_waits\":%u,\"output_busy\":%u,\"silence_blocks\":%u,\"max_drive_wait_ms\":%u,\"max_output_busy_ms\":%u}}}", CD_STREAM_SERVICE, CD_STREAM_VERSION, CD_STREAM_PORT, AUDIO_RATE, CD_STREAM_CHANNELS, CD_STREAM_BITS, server->disconnects, server->send_retries, server->resets, server->mailbox_overflows, server->max_lag_blocks, server->max_poll_gap_ms, server->last_disconnect, server->last_error, server->source.blocks, server->source.gaps, server->source.max_gap_ms, server->source.drive_waits, server->source.output_busy, server->source.silence_blocks, server->source.max_drive_wait_ms, server->source.max_output_busy_ms);

        if (bytes < 0 || (unsigned)bytes >= sizeof(reply))
        {
            return CD_STREAM_SERVER_ERROR_SOCKET;
        }

        int sent = sendto(server->discovery, reply, bytes, 0, (struct sockaddr*)&peer, length);

        if (sent != bytes && (sent >= 0 || !retry(server->discovery)))
        {
            return CD_STREAM_SERVER_ERROR_SOCKET;
        }
    }

    if (n == (int)strlen(CD_STREAM_TRACE) && !memcmp(query, CD_STREAM_TRACE, (unsigned)n) &&
        length == sizeof(peer) && peer.sin_family == AF_INET && peer.sin_port && peer.sin_addr.s_addr)
    {
        char                 reply[DISCOVERY_BYTES];
        const Ps2NetTxStats* driver = &server->stall_driver;
        const Ps2TcpState*   tcp    = &server->stall_tcp;
        int                  bytes  = snprintf(reply, sizeof(reply), "{\"service\":\"%s\",\"traceVersion\":1,\"id\":%u,\"duration_ms\":%u,\"sample_ms\":%u,\"lag\":%u,\"available\":%u,\"tcp\":{\"flags\":%u,\"state\":%u,\"last_ack\":%u,\"next_sequence\":%u,\"last_written\":%u,\"send_space\":%u,\"queue_length\":%u,\"unsent_segments\":%u,\"unsent_bytes\":%u,\"unsent_ref\":%u,\"unacked_segments\":%u,\"unacked_bytes\":%u,\"unacked_ref\":%u,\"congestion_window\":%u,\"peer_window\":%u,\"retransmissions\":%u,\"rto_ms\":%u,\"timer_ms\":%d,\"persist_backoff\":%u},\"driver\":{\"status\":%d,\"link_mode\":%u,\"tx_dropped\":%u,\"tx_errors\":%u,\"tx_carrier\":%u,\"tx_deferred\":%u,\"tx_collisions\":%u,\"tx_underruns\":%u,\"rx_alloc_fail\":%u,\"rx_overrun\":%u,\"rx_bad_fcs\":%u}}", CD_STREAM_SERVICE, server->stall_id, server->stall_ms, server->stall_sample_ms, server->stall_lag, server->tcp_available, (unsigned)tcp->flags, (unsigned)tcp->state, (unsigned)tcp->last_ack, (unsigned)tcp->next_sequence, (unsigned)tcp->last_written, (unsigned)tcp->send_space, (unsigned)tcp->queue_length, (unsigned)tcp->unsent_segments, (unsigned)tcp->unsent_bytes, (unsigned)tcp->unsent_ref, (unsigned)tcp->unacked_segments, (unsigned)tcp->unacked_bytes, (unsigned)tcp->unacked_ref, (unsigned)tcp->congestion_window, (unsigned)tcp->peer_window, (unsigned)tcp->retransmissions, (unsigned)tcp->rto_ms, (int)tcp->timer_ms, (unsigned)tcp->persist_backoff, (int)driver->status, (unsigned)driver->link_mode, (unsigned)driver->tx_dropped, (unsigned)driver->tx_errors, (unsigned)driver->tx_carrier, (unsigned)driver->tx_deferred, (unsigned)driver->tx_collisions, (unsigned)driver->tx_underruns, (unsigned)driver->rx_alloc_fail, (unsigned)driver->rx_overrun, (unsigned)driver->rx_bad_fcs);

        if (bytes < 0 || (unsigned)bytes >= sizeof(reply))
        {
            return CD_STREAM_SERVER_ERROR_SOCKET;
        }

        int sent = sendto(server->discovery, reply, bytes, 0, (struct sockaddr*)&peer, length);

        if (sent != bytes && (sent >= 0 || !retry(server->discovery)))
        {
            return CD_STREAM_SERVER_ERROR_SOCKET;
        }
    }

    int fd = accept(server->listener, NULL, NULL);

    if (fd < 0 && !retry(server->listener))
    {
        return CD_STREAM_SERVER_ERROR_SOCKET;
    }

    if (fd >= 0)
    {
        unsigned slot = 0;

        while (slot < CD_STREAM_CLIENTS && server->clients[slot].fd >= 0)
        {
            ++slot;
        }

        if (slot == CD_STREAM_CLIENTS || server->nonblocking(fd) != 0)
        {
            close(fd);
        }
        else
        {
            int enabled = 1;

            /* Best effort: avoid Nagle delaying partial-sector packets in the small IOP send queue. */
            (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));

            server->clients[slot].fd       = fd;
            server->clients[slot].progress = now;
        }
    }

    for (unsigned i = 0; i < CD_STREAM_CLIENTS; ++i)
    {
        CdStreamClient* client = &server->clients[i];

        if (client->fd < 0)
        {
            continue;
        }

        if (!client->subscribed)
        {
            if ((uint32_t)(now - client->progress) >= HANDSHAKE_TIMEOUT_MS)
            {
                close_client(client);
            }
            else
            {
                handshake(server, client, now);
            }
        }
        else
        {
            char probe;
            int  received = recv(client->fd, &probe, 1, MSG_PEEK);

            if (received >= 0)
            {
                close_client(client);
            }
            else if (!retry(client->fd))
            {
                drop_client(server, client, "receive_error", platform_socket_error(client->fd));
            }
        }

        if (client->fd >= 0 && client->subscribed)
        {
            send_audio(server, client, now);
        }
    }

    return 0;
}

void cd_stream_server_push(CdStreamServer* server, const uint8_t* pcm)
{
    memcpy(server->blocks[server->write % CD_STREAM_BLOCKS], pcm, CD_STREAM_MAX_PAYLOAD_BYTES);
    ++server->write;

    if (server->write > CD_STREAM_BLOCKS)
    {
        server->opening = 0;
    }

    server->finished = 0;
}

void cd_stream_server_reset(CdStreamServer* server)
{
    if (cd_stream_server_clients(server))
    {
        ++server->resets;

        server->last_disconnect = "playback_reset";
        server->last_error      = 0;
    }

    for (unsigned i = 0; i < CD_STREAM_CLIENTS; ++i)
    {
        close_client(&server->clients[i]);
    }

    server->write    = 0;
    server->finished = 0;
    server->opening  = 1;
}

void cd_stream_server_end(CdStreamServer* server)
{
    server->finished = 1;
    server->opening  = 0;
}

unsigned cd_stream_server_clients(const CdStreamServer* server)
{
    unsigned count = 0;

    for (unsigned i = 0; i < CD_STREAM_CLIENTS; ++i)
    {
        count += server->clients[i].fd >= 0 && server->clients[i].subscribed;
    }

    return count;
}

void cd_stream_server_overflows(CdStreamServer* server, unsigned count)
{
    server->mailbox_overflows = count;
    server->last_disconnect   = "mailbox_overflow";
    server->last_error        = 0;
}

void cd_stream_server_close(CdStreamServer* server)
{
    cd_stream_server_reset(server);

    if (server->listener >= 0)
    {
        close(server->listener);

        server->listener = -1;
    }

    if (server->discovery >= 0)
    {
        close(server->discovery);

        server->discovery = -1;
    }
}
