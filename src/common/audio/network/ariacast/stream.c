#include "audio/network/ariacast/stream.h"
#include <string.h>

int aria_stream_active(const AriaStream* stream, uint32_t now)
{
    return !stream->finished && (stream->connected || stream->ending) && stream->has_pcm &&
           (uint32_t)(now - (stream->ending ? stream->ending_at : stream->last_pcm)) < ARIA_IDLE_MS;
}

int aria_stream_push(AriaStream* stream, const uint8_t* data, unsigned size, uint32_t now)
{
    if (stream->ending)
    {
        return ARIA_STREAM_ERROR_ENDING;
    }

    if (size != ARIA_PCM_BYTES)
    {
        return ARIA_STREAM_ERROR_PCM_SIZE;
    }

    if (stream->count == ARIA_QUEUE_MESSAGES)
    {
        return ARIA_STREAM_ERROR_QUEUE_FULL;
    }

    memcpy(stream->pcm[(stream->read + stream->count) % ARIA_QUEUE_MESSAGES], data, size);
    ++stream->count;
    ++stream->received;

    stream->has_pcm  = 1;
    stream->last_pcm = now;

    return 0;
}

void aria_stream_consume(AriaStream* stream)
{
    if (stream->count)
    {
        stream->read = (stream->read + 1) % ARIA_QUEUE_MESSAGES;

        --stream->count;
    }
}
