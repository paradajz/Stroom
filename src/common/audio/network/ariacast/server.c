#include "audio/network/ariacast/server.h"
#include "audio/network/ariacast/client.h"
#include "audio/network/ariacast/discovery.h"
#include "audio/network/ariacast/socket.h"
#include "audio/network/ariacast/diagnostics/poll.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define SOCKET_POLL_MS    10
#define DISCOVERY_POLL_MS 100

/**
 * @brief Forward a playback failure to the owning transport.
 * @param context Receiver.
 * @param reason Failure description.
 */
static void stream_disconnect(void* context, const char* reason)
{
    aria_server_disconnect(context, reason);
}

void aria_server_disconnect(AriaServer* server, const char* reason)
{
    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        if (server->clients[i].fd >= 0 && server->clients[i].fd == server->audio_fd)
        {
            aria_client_close(server, &server->clients[i], reason, 0);
        }
    }

    if (server->stream.ending)
    {
        server->stream.ending = server->stream.finished = 0;
        server->stream.count = server->stream.read = 0;
        server->stream.listening                   = 0;
        server->stream.device_name[0]              = 0;

        ++server->stream.generation;
        /* Metadata may already have arrived for the next connection. */
    }
}

int aria_server_open(AriaServer* server, int (*nonblocking)(int fd))
{
    memset(server, 0, sizeof(*server));

    server->listener = server->discovery = server->audio_fd = -1;
    server->nonblocking                                     = nonblocking;
    server->stream.disconnect                               = stream_disconnect;
    server->stream.context                                  = server;

    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        server->clients[i].fd = -1;
    }

    server->listener = aria_socket_bind(server, SOCK_STREAM, ARIA_STREAM_PORT);

    if (server->listener >= 0 && listen(server->listener, ARIA_CLIENTS) < 0)
    {
        aria_socket_error(server, SOCK_STREAM, ARIA_STREAM_PORT, "LISTEN", errno);
    }

    if (!server->error[0])
    {
        server->discovery = aria_socket_bind(server, SOCK_DGRAM, ARIA_DISCOVERY_PORT);
    }

    if (server->error[0])
    {
        aria_server_close(server);
        return ARIA_SERVER_ERROR_OPEN;
    }

    return 0;
}

int aria_server_active(const AriaServer* server, uint32_t now)
{
    return aria_stream_active(&server->stream, now);
}

void aria_server_step(AriaServer* server, uint32_t now, const char* address, int permitted)
{
    aria_diagnostic_poll(server, now);

    uint32_t poll_gap = now - server->last_poll;

#if STROOM_DIAGNOSTICS
    if (server->last_poll && poll_gap > server->diagnostics.max_poll_gap)
    {
        server->diagnostics.max_poll_gap = poll_gap;
    }
#endif
    server->permitted = permitted;

    if (permitted && server->audio_fd >= 0 && server->stream.has_pcm && !aria_server_active(server, now))
    {
        for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
        {
            if (server->clients[i].fd == server->audio_fd)
            {
                aria_client_end(server, &server->clients[i], "PCM receive timeout", 0, now);
                break;
            }
        }
    }

    if (!permitted || (server->stream.ending && !aria_server_active(server, now)))
    {
#if STROOM_DIAGNOSTICS
        int had_audio = server->audio_fd >= 0;
#endif
        aria_server_disconnect(server, permitted ? "playback drain ended" : "source ownership changed");

#if STROOM_DIAGNOSTICS
        if (had_audio && server->stream.has_pcm)
        {
            server->diagnostics.last_disconnect_gap = now - server->stream.last_pcm;
        }
#endif
    }

    if (server->listener < 0)
    {
        return;
    }

    /* Bound RPC traffic without relying on readiness reports from the
     * launcher-provided IOP stack through the EE select bridge. */

    if (poll_gap < SOCKET_POLL_MS)
    {
        return;
    }

    server->last_poll = now;

    aria_clients_tick(server, now);

    if ((uint32_t)(now - server->last_discovery_poll) >= DISCOVERY_POLL_MS)
    {
        server->last_discovery_poll = now;

        if (aria_discovery_step(server, address) != 0)
        {
            aria_server_close(server);
            return;
        }

        server->operation = "ACCEPT";

        struct sockaddr_in peer      = { 0 };
        socklen_t          peer_size = sizeof(peer);
        int                fd        = accept(server->listener, (struct sockaddr*)&peer, &peer_size);

        if (fd < 0 && !aria_socket_retry(server->listener))
        {
            aria_socket_error(server, SOCK_STREAM, ARIA_STREAM_PORT, "ACCEPT", errno);
            aria_server_close(server);
            return;
        }

        if (fd >= 0)
        {
            unsigned slot = 0;

            while (slot < ARIA_CLIENTS && server->clients[slot].fd >= 0)
            {
                ++slot;
            }

            if (slot == ARIA_CLIENTS || server->nonblocking(fd) != 0)
            {
                close(fd);
            }
            else
            {
                server->clients[slot].fd           = fd;
                server->clients[slot].touched      = now;
                server->clients[slot].heartbeat_at = now;
                server->clients[slot].peer_address = peer.sin_addr.s_addr;
            }
        }
    }

    aria_clients_service(server, now);
}

void aria_server_close(AriaServer* server)
{
    aria_server_disconnect(server, "receiver shutdown");

    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        aria_client_close(server, &server->clients[i], "receiver shutdown", 0);
    }

    if (server->listener >= 0)
    {
        close(server->listener);
    }

    if (server->discovery >= 0)
    {
        close(server->discovery);
    }

    server->listener = server->discovery = -1;
}
