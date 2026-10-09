#include "audio/network/ariacast/discovery.h"
#include "audio/network/ariacast/socket.h"
#include "audio/network/ariacast/diagnostics/requests.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define JSON_REPLY_BYTES     2048
#define DISCOVERY_READ_BYTES 128

int aria_discovery_step(AriaServer* server, const char* address)
{
    char query[DISCOVERY_READ_BYTES + 1];

    struct sockaddr_in peer   = { 0 };
    socklen_t          length = sizeof(peer);
    int                n;

    server->operation = "DISCOVERY READ";
    n                 = recvfrom(server->discovery, query, DISCOVERY_READ_BYTES, 0, (struct sockaddr*)&peer, &length);

    if (n < 0 && !aria_socket_retry(server->discovery))
    {
        aria_socket_error(server, SOCK_DGRAM, ARIA_DISCOVERY_PORT, "RECEIVE", errno);
        return ARIA_DISCOVERY_ERROR_RECEIVE;
    }

    if (n > 0)
    {
#if STROOM_DIAGNOSTICS
        ++server->diagnostics.discovery_received;
#endif

        while (n && (query[n - 1] == '\r' || query[n - 1] == '\n' || query[n - 1] == ' '))
        {
            --n;
        }

        query[n] = 0;

        if (length == sizeof(peer) && peer.sin_family == AF_INET && peer.sin_port && peer.sin_addr.s_addr && aria_diagnostic_request(server, query, &peer))
        {
            return 0;
        }

        if (!strcmp(query, "DISCOVER_AUDIOCAST") && address[0])
        {
#if STROOM_DIAGNOSTICS
            ++server->diagnostics.discovery_matched;
#endif
            char reply[JSON_REPLY_BYTES];
            int  bytes = snprintf(reply, sizeof(reply), "{\"server_name\":\"Stroom\",\"ip\":\"%s\",\"port\":%d,\"samplerate\":%u,\"channels\":%u}", address, ARIA_STREAM_PORT, (unsigned)AUDIO_RATE, (unsigned)ARIA_PCM_CHANNELS);

            server->operation = "DISCOVERY REPLY";

            if (length != sizeof(peer) || peer.sin_family != AF_INET || !peer.sin_port || !peer.sin_addr.s_addr)
            {
                snprintf(server->error, sizeof(server->error), "ARIA DISCOVERY INVALID PEER");
            }
            else if (sendto(server->discovery, reply, bytes, 0, (struct sockaddr*)&peer, length) != bytes)
            {
                aria_socket_error(server, SOCK_DGRAM, ARIA_DISCOVERY_PORT, "REPLY", errno);
            }
            else
            {
#if STROOM_DIAGNOSTICS
                ++server->diagnostics.discovery_replied;
#endif
            }
        }
    }

    return 0;
}
