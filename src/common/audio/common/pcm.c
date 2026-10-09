#include "audio/common/pcm.h"
#include <math.h>
#include <string.h>

/**
 * @brief Decode an unsigned little-endian 16-bit integer.
 *
 * @param p Pointer to at least two bytes.
 * @return Decoded value.
 */
static unsigned get16(const uint8_t* p)
{
    return p[0] | (unsigned)p[1] << 8;
}

void audio_push_pcm(AudioBuffer* a, const uint8_t* pcm, unsigned frames, uint32_t now_ms)
{
    if (!frames)
    {
        return;
    }

    if (frames > AUDIO_CAPACITY)
    {
        pcm += (frames - AUDIO_CAPACITY) * 4;
        frames = AUDIO_CAPACITY;
    }

    if (a->count + frames > AUDIO_CAPACITY)
    {
        unsigned discard = a->count + frames - AUDIO_CAPACITY;

        a->read = (a->read + discard) % AUDIO_CAPACITY;
        a->count -= discard;
    }

    for (unsigned i = 0; i < frames; ++i)
    {
        unsigned dst = (a->read + a->count) % AUDIO_CAPACITY;

        for (int ch = 0; ch < 2; ++ch)
        {
            unsigned raw = get16(pcm + 4 * i + 2 * ch);

            a->ring[dst][ch]                                   = (int16_t)(raw <= INT16_MAX ? (int)raw : (int)raw - ((int)UINT16_MAX + 1));
            a->snapshot.history[a->snapshot.history_write][ch] = a->ring[dst][ch];
        }

        ++a->count;

        a->snapshot.history_write = (a->snapshot.history_write + 1) % AUDIO_HISTORY;

        if (a->snapshot.history_count < AUDIO_HISTORY)
        {
            ++a->snapshot.history_count;
        }
    }

    a->last_ms         = now_ms;
    a->snapshot.active = 1;
}

void audio_analyze(AudioBuffer* a, uint32_t now_ms)
{
    if (!a->snapshot.active || (uint32_t)(now_ms - a->last_ms) >= AUDIO_STALE_MS)
    {
        a->snapshot.active = 0;
        a->read = a->count        = 0;
        a->snapshot.history_write = a->snapshot.history_count = 0;
        a->snapshot.rms[0] = a->snapshot.rms[1] = a->snapshot.peak[0] = a->snapshot.peak[1] = 0;

        return;
    }

    if (!a->count)
    {
        return;
    }

    float    sum[2] = { 0, 0 }, peak[2] = { 0, 0 };
    unsigned count = a->count;

    while (a->count)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float v = (float)a->ring[a->read][ch] / AUDIO_PCM16_SCALE;

            sum[ch] += v * v;

            if (fabsf(v) > peak[ch])
            {
                peak[ch] = fabsf(v);
            }
        }

        a->read = (a->read + 1) % AUDIO_CAPACITY;

        --a->count;
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        a->snapshot.rms[ch]  = sqrtf(sum[ch] / count);
        a->snapshot.peak[ch] = peak[ch];
    }
}
