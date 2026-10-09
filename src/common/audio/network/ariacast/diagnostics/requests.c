#include "audio/network/ariacast/diagnostics/requests.h"
#include "platform/network/rpc/driver_stats.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define JSON_REPLY_BYTES 2048

/* Eight uint32 words, commas and brackets require at most 90 bytes per row. */
#define COVER_TIMING_JSON_BYTES (128 + DIAGNOSTIC_ARTWORK_PHASE_MAX * 90)

int aria_diagnostic_request(AriaServer* server, const char* query, const struct sockaddr_in* peer)
{
    if (!strncmp(query, DIAGNOSTIC_CAPTURE_QUERY, sizeof(DIAGNOSTIC_CAPTURE_QUERY) - 1))
    {
        char reply[JSON_REPLY_BYTES];

        if (peer->sin_family == AF_INET && peer->sin_port && peer->sin_addr.s_addr && aria_diagnostic_capture_reply(&server->diagnostics.diagnostic_capture, query, reply, sizeof(reply)))
        {
            sendto(server->discovery, reply, strlen(reply), 0, (const struct sockaddr*)peer, sizeof(*peer));
        }
    }
    else if (!strcmp(query, DIAGNOSTIC_DRIVER_QUERY))
    {
        if (peer->sin_family == AF_INET && peer->sin_port && peer->sin_addr.s_addr)
        {
            Ps2NetDriverStats driver;

            platform_net_driver_stats(&driver);

            char reply[JSON_REPLY_BYTES];

            snprintf(reply, sizeof(reply), "{\"driverDiagnosticVersion\":1,\"status\":%d,\"linkUp\":%u,\"linkMode\":%u,\"rxAllocFail\":%u,\"rxOverrun\":%u,\"rxBadLength\":%u,\"rxBadFcs\":%u,\"rxBadAlignment\":%u,\"rxDropped\":%u,\"rxErrors\":%u}", (int)driver.status, (unsigned)driver.link_up, (unsigned)driver.link_mode, (unsigned)driver.rx_alloc_fail, (unsigned)driver.rx_overrun, (unsigned)driver.rx_bad_length, (unsigned)driver.rx_bad_fcs, (unsigned)driver.rx_bad_alignment, (unsigned)driver.rx_dropped, (unsigned)driver.rx_errors);
            sendto(server->discovery, reply, strlen(reply), 0, (const struct sockaddr*)peer, sizeof(*peer));
        }
    }
    else if (!strcmp(query, DIAGNOSTIC_METADATA_QUERY))
    {
        if (peer->sin_family == AF_INET && peer->sin_port && peer->sin_addr.s_addr)
        {
            sendto(server->discovery, server->diagnostics.metadata_request, server->diagnostics.metadata_request_size, 0, (const struct sockaddr*)peer, sizeof(*peer));
        }
    }
    else if (!strcmp(query, DIAGNOSTIC_ARTWORK_QUERY))
    {
        if (peer->sin_family == AF_INET && peer->sin_port && peer->sin_addr.s_addr)
        {
            char reply[TRACK_METADATA_JSON_BYTES + sizeof(server->diagnostics.artwork_fetch) + sizeof(server->diagnostics.artwork_display) + COVER_TIMING_JSON_BYTES];

            track_metadata_to_json(&server->metadata, reply);

            size_t used = strlen(reply) - 1;

            snprintf(reply + used, sizeof(reply) - used, ",\"artworkDiagnosticVersion\":1,\"fetch\":\"%s\",\"display\":\"%s\"}", server->diagnostics.artwork_fetch, server->diagnostics.artwork_display);

            used = strlen(reply) - 1;
            used += snprintf(reply + used, sizeof(reply) - used, ",\"coverTimingRevision\":%u,\"coverTiming\":[", server->diagnostics.cover_revision);

            int separator = 0;

            for (unsigned i = 0; i < DIAGNOSTIC_ARTWORK_PHASE_MAX; ++i)
            {
                if (!server->diagnostics.cover_phases[i].count)
                {
                    continue;
                }

                const AriaDiagnosticCaptureRecord* r = &server->diagnostics.cover_phases[i].last;

                used += snprintf(reply + used, sizeof(reply) - used, "%s[%u,%u,%u,%u,%u,%u,%u,%u]", separator ? "," : "", i + 1, server->diagnostics.cover_phases[i].count, (unsigned)server->diagnostics.cover_phases[i].maximum, (unsigned)(r->at - r->data[DIAGNOSTIC_CAPTURE_COVER_DURATION_MS]), (unsigned)r->at, (unsigned)r->data[DIAGNOSTIC_CAPTURE_COVER_VALUE0], (unsigned)r->data[DIAGNOSTIC_CAPTURE_COVER_VALUE1], (unsigned)r->data[DIAGNOSTIC_CAPTURE_COVER_VALUE2]);
                separator = 1;
            }

            snprintf(reply + used, sizeof(reply) - used, "]}");
            sendto(server->discovery, reply, strlen(reply), 0, (const struct sockaddr*)peer, sizeof(*peer));
        }
    }
    else if (!strcmp(query, DIAGNOSTIC_QUERY))
    {
        if (peer->sin_family == AF_INET && peer->sin_port && peer->sin_addr.s_addr)
        {
            char reply[JSON_REPLY_BYTES];

            aria_statistics(server, reply, sizeof(reply));

            server->operation = "DIAGNOSTIC REPLY";

            /* Best effort: a diagnostic reader must not interrupt audio on send failure. */
            sendto(server->discovery, reply, strlen(reply), 0, (const struct sockaddr*)peer, sizeof(*peer));
        }
    }

    else
    {
        return 0;
    }

    return 1;
}
