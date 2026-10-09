#include "audio/network/ariacast/server.h"
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>

void aria_statistics(const AriaServer* server, char* stats, size_t capacity)
{
#if STROOM_DIAGNOSTICS
    unsigned clients = 0;

    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        if (server->clients[i].fd >= 0)
        {
            ++clients;
        }
    }

    snprintf(stats, capacity, "{\"diagnosticVersion\":1,\"overflowPolicy\":\"backpressure\",\"graceMs\":%u,\"clients\":%u,\"receivedFrames\":%u,\"bufferedFrames\":%u,\"queuedFrames\":%u,\"droppedFrames\":%u,\"discoveryReceived\":%u,\"discoveryMatched\":%u,\"discoveryReplied\":%u,\"pings\":%u,\"pongs\":%u,\"heartbeatTimeouts\":%u,\"lastPongIPv4\":%u,\"disconnects\":%u,\"lastDisconnect\":\"%s\",\"disconnectError\":%d,\"disconnectQueued\":%u,\"disconnectGapMs\":%u,\"maxPollGapMs\":%u,\"maxPcmGapMs\":%u}", (unsigned)ARIA_IDLE_MS, clients, server->stream.received, server->stream.count, server->stream.count, 0u, server->diagnostics.discovery_received, server->diagnostics.discovery_matched, server->diagnostics.discovery_replied, server->diagnostics.pings, server->diagnostics.pongs, server->diagnostics.heartbeat_timeouts, (unsigned)ntohl(server->diagnostics.last_pong_address), server->diagnostics.disconnects, server->diagnostics.last_disconnect, server->diagnostics.last_disconnect_error, server->diagnostics.last_disconnect_queued, (unsigned)server->diagnostics.last_disconnect_gap, (unsigned)server->diagnostics.max_poll_gap, (unsigned)server->diagnostics.max_pcm_gap);

    const AriaDiagnostics* d    = &server->diagnostics;
    size_t                 used = strlen(stats);

    if (used && stats[used - 1] == '}')
    {
        snprintf(stats + used - 1, capacity - used + 1, ",\"audioDiagnosticVersion\":1,\"emptyAudioReads\":%u,\"pausedAudioReads\":%u,\"maxByteGapMs\":%u,\"pcmWrites\":%u,\"silenceWrites\":%u,\"busyWrites\":%u,\"outputErrors\":%u,\"soundQueuedBytes\":%d,\"emptyOutputWithPcm\":%u,\"emptyOutputWithoutPcm\":%u,\"maxWriteGapMs\":%u,\"maxSocketMs\":%u,\"maxPlaybackMs\":%u,\"maxArtworkMs\":%u}", d->empty_reads, d->paused_reads, (unsigned)d->max_byte_gap, d->pcm_writes, d->silence_writes, d->busy_writes, d->output_errors, d->sound_queued, d->empty_with_pcm, d->empty_without_pcm, (unsigned)d->max_write_gap, (unsigned)d->max_socket_ms, (unsigned)d->max_playback_ms, (unsigned)d->max_artwork_ms);
    }
#else
    snprintf(stats, capacity, "{\"receivedFrames\":%u,\"bufferedFrames\":%u,\"queuedFrames\":%u,\"droppedFrames\":0}", server->stream.received, server->stream.count, server->stream.count);
#endif
}
