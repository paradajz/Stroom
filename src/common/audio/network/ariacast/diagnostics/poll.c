#include "audio/network/ariacast/diagnostics/poll.h"
#include "platform/network/diagnostic/wire.h"
#include "platform/time/clock.h"
#include <string.h>

#define DIAGNOSTIC_POLL_MS   20
#define DIAGNOSTIC_SAMPLE_MS 50

void aria_diagnostic_poll(AriaServer* server, uint32_t now)
{
    AriaDiagnostics* d      = &server->diagnostics;
    int              select = !d->diagnostic_capture_initialized || d->diagnostic_capture.session != server->stream.generation;

    if (select)
    {
        aria_diagnostic_capture_session(&d->diagnostic_capture, server->stream.generation);

        d->diagnostic_capture_initialized = 1;
    }

    if (select || (server->audio_fd >= 0 && (uint32_t)(now - d->diagnostic_capture_poll_at) >= DIAGNOSTIC_POLL_MS))
    {
        d->diagnostic_capture_poll_at = now;

        Ps2DiagnosticRequest request = { .abi = DIAGNOSTIC_IOP_ABI, .command = select ? DIAGNOSTIC_IOP_SELECT : DIAGNOSTIC_IOP_POLL, .socket = server->audio_fd };
        Ps2DiagnosticReply   reply;
        uint32_t             begin = platform_millis();

        platform_net_diagnostic(&request, &reply);

        uint32_t end = platform_millis();

        d->diagnostic_capture.iop_status = reply.status;

        if (reply.status == 1 && (reply.abi != DIAGNOSTIC_IOP_ABI || (server->audio_fd >= 0 && (!reply.peer || !reply.port))))
        {
            d->diagnostic_capture.iop_status = PS2_DIAGNOSTIC_ERROR_ABI;
        }

        d->diagnostic_capture.iop_lost = reply.lost;

        AriaDiagnosticCaptureRecord clock = { .at = end, .kind = DIAGNOSTIC_CAPTURE_CLOCK, .data = { [DIAGNOSTIC_CAPTURE_CLOCK_EE_BEGIN] = begin, [DIAGNOSTIC_CAPTURE_CLOCK_EE_END] = end, [DIAGNOSTIC_CAPTURE_CLOCK_IOP_NOW] = reply.now, [DIAGNOSTIC_CAPTURE_CLOCK_IOP_LOST] = reply.lost, [DIAGNOSTIC_CAPTURE_CLOCK_STATUS] = (uint32_t)reply.status, [DIAGNOSTIC_CAPTURE_CLOCK_ABI] = reply.abi } };

        aria_diagnostic_capture_record(&d->diagnostic_capture, clock);

        if (select)
        {
            d->diagnostic_capture.peer      = reply.peer;
            d->diagnostic_capture.port      = reply.port;
            d->diagnostic_capture.byte_base = server->diagnostics.audio_bytes;

            AriaDiagnosticCaptureRecord connection = { .at = end, .kind = DIAGNOSTIC_CAPTURE_CONNECTION, .data = { [DIAGNOSTIC_CAPTURE_CONNECTION_PEER_IPV4] = reply.peer, [DIAGNOSTIC_CAPTURE_CONNECTION_PEER_PORT] = reply.port, [DIAGNOSTIC_CAPTURE_CONNECTION_SOCKET] = (uint32_t)server->audio_fd, [DIAGNOSTIC_CAPTURE_CONNECTION_SESSION] = server->stream.generation, [DIAGNOSTIC_CAPTURE_CONNECTION_SOCKET_BYTES] = server->diagnostics.audio_bytes, [DIAGNOSTIC_CAPTURE_CONNECTION_PCM_MESSAGES] = server->stream.received } };

            aria_diagnostic_capture_record(&d->diagnostic_capture, connection);
        }

        if (reply.status == 1 && reply.abi == DIAGNOSTIC_IOP_ABI && reply.count <= DIAGNOSTIC_IOP_BATCH)
        {
            d->diagnostic_capture.iop_records += reply.count;

            uint32_t midpoint = begin + (uint32_t)(end - begin) / 2;

            for (unsigned i = 0; i < reply.count; ++i)
            {
                AriaDiagnosticCaptureRecord record = { .at = midpoint + (uint32_t)(reply.records[i].at - reply.now), .kind = reply.records[i].kind };

                memcpy(record.data, reply.records[i].data, sizeof(record.data));

                if (record.kind == DIAGNOSTIC_IOP_SACK_STATE)
                {
                    d->diagnostic_capture.sack_known      = 1;
                    d->diagnostic_capture.sack_enabled    = record.data[DIAGNOSTIC_IOP_SACK_STATE_ENABLED];
                    d->diagnostic_capture.sack_negotiated = record.data[DIAGNOSTIC_IOP_SACK_STATE_NEGOTIATED];
                }

                aria_diagnostic_capture_record(&d->diagnostic_capture, record);
            }
        }
    }

    if (server->audio_fd >= 0 && (uint32_t)(now - d->diagnostic_capture_sample_at) >= DIAGNOSTIC_SAMPLE_MS)
    {
        d->diagnostic_capture_sample_at = now;

        unsigned pending = 0;

        for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
        {
            if (server->clients[i].fd == server->audio_fd)
            {
                pending = server->clients[i].websocket.used;

                break;
            }
        }

        AriaDiagnosticCaptureRecord sample = { .at = now, .kind = DIAGNOSTIC_CAPTURE_SAMPLE, .data = { [DIAGNOSTIC_CAPTURE_SAMPLE_PCM_MS] = server->stream.count * ARIA_PCM_MESSAGE_MS, [DIAGNOSTIC_CAPTURE_SAMPLE_PCM_MESSAGES] = server->stream.received, [DIAGNOSTIC_CAPTURE_SAMPLE_READ_ATTEMPTS] = server->diagnostics.audio_reads, [DIAGNOSTIC_CAPTURE_SAMPLE_SOCKET_BYTES] = server->diagnostics.audio_bytes, [DIAGNOSTIC_CAPTURE_SAMPLE_PARTIAL_MESSAGE_BYTES] = pending, [DIAGNOSTIC_CAPTURE_SAMPLE_PAUSED_READS] = d->paused_reads } };

        aria_diagnostic_capture_record(&d->diagnostic_capture, sample);
    }

    aria_diagnostic_capture_tick(&d->diagnostic_capture, platform_millis());
}
