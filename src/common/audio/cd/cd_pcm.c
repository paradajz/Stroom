#include "audio/cd/cd_pcm.h"

/* Other output rates require validating interpolation and integer bounds first. */
// NOLINTNEXTLINE(readability-magic-numbers) -- Independent assertion of the validated resampling rate.
_Static_assert(AUDIO_RATE == 48000, "CD resampling currently supports 48 kHz output only");
_Static_assert(AUDIO_RATE % CD_SECTORS_PER_SECOND == 0, "CD output must contain whole frames per sector");

/**
 * @brief Decode one signed little-endian PCM16 sample.
 *
 * @param p Pointer to at least two bytes.
 * @return Signed sample value.
 */
static int16_t sample(const uint8_t* p)
{
    unsigned v = p[0] | (unsigned)p[1] << 8;

    return (int16_t)(v <= INT16_MAX ? (int)v : (int)v - ((int)UINT16_MAX + 1));
}

void cd_resample(CdResampler* state, const uint8_t* sector, uint8_t* output)
{
    if (!state->primed)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            state->previous[ch] = sample(sector + ch * 2);
        }

        state->primed = 1;
    }

    /* Causal linear interpolation, one input sample delay. Exact rational
     * phase returns to zero each sector; carry the last sample across reads. */

    for (unsigned i = 0; i < CD_OUTPUT_FRAMES; ++i)
    {
        unsigned phase = i * CD_INPUT_FRAMES, index = phase / CD_OUTPUT_FRAMES, fraction = phase % CD_OUTPUT_FRAMES;

        for (int ch = 0; ch < 2; ++ch)
        {
            int      a = index ? sample(sector + (index - 1) * 4 + ch * 2) : state->previous[ch];
            int      b = sample(sector + index * 4 + ch * 2);
            uint16_t v = (uint16_t)(int16_t)(a + (b - a) * (int)fraction / CD_OUTPUT_FRAMES);

            output[i * 4 + ch * 2]     = v;
            output[i * 4 + ch * 2 + 1] = v >> 8;
        }
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        state->previous[ch] = sample(sector + (CD_INPUT_FRAMES - 1) * 4 + ch * 2);
    }
}
