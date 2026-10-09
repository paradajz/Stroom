/* Audio conditioning adapted from MilkDrop AnalyzeNewSound/AlignWaves. */
/*
  LICENSE
  -------
Copyright 2005-2013 Nullsoft, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  * Neither the name of Nullsoft nor the names of its contributors may be used to
    endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "milkdrop/milk_audio.h"
#include "milkdrop/fft_real_start.h"
#include <math.h>
#include <string.h>

#define FFT_SIZE                     (MILK_AUDIO_BINS * 2)
#define REFERENCE_AUDIO_RATE         44100.0f
#define ALIGNMENT_LEVELS             6
#define ALIGNMENT_MAX_SHIFT          96
#define ALIGNMENT_SAMPLES            (MILK_AUDIO_SAMPLES - ALIGNMENT_MAX_SHIFT)
#define ALIGNMENT_INITIAL_CANDIDATES 3

/* Sum of the six halving levels; base lengths must divide evenly through the last level. */
#define ALIGNMENT_STORAGE(samples) (2 * (samples) - ((samples) >> (ALIGNMENT_LEVELS - 1)))
#define REFERENCE_PCM_SCALE        256.0f

/* Two neighboring input samples are needed for the oldest interpolated point. */
#define INPUT_HISTORY_REQUIRED ((unsigned)((MILK_AUDIO_SAMPLES - 1) * (AUDIO_RATE / REFERENCE_AUDIO_RATE)) + 2)

#define PI 3.14159265358979323846f

static float window[MILK_AUDIO_SAMPLES], weight[MILK_AUDIO_BINS];
static float cosine[MILK_AUDIO_BINS], sine[MILK_AUDIO_BINS];
/* Fixed AlignWaves taper; identical arithmetic, computed only at startup. */
static float    alignment_weight[ALIGNMENT_STORAGE(ALIGNMENT_SAMPLES)];
static unsigned alignment_offset[ALIGNMENT_LEVELS], alignment_weight_offset[ALIGNMENT_LEVELS];
static uint16_t fft_swaps[FFT_SIZE / 2][2];
static unsigned fft_swap_count;
static int      initialized;

_Static_assert(MILK_AUDIO_SAMPLES % (1 << (ALIGNMENT_LEVELS - 1)) == 0, "Alignment samples must halve evenly");
_Static_assert(ALIGNMENT_SAMPLES % (1 << (ALIGNMENT_LEVELS - 1)) == 0, "Alignment weights must halve evenly");
_Static_assert(FFT_SIZE <= UINT16_MAX, "FFT swap indices must fit uint16_t");

/**
 * @brief Initialize reference FFT and waveform-alignment tables once.
 */
static void initialize(void)
{
    if (initialized)
    {
        return;
    }

    for (unsigned i = 0; i < MILK_AUDIO_SAMPLES; ++i)
    {
        window[i] = .5f - .5f * cosf(2 * PI * i / MILK_AUDIO_SAMPLES);
    }

    for (unsigned i = 0; i < MILK_AUDIO_BINS; ++i)
    {
        weight[i] = -.02f * logf((MILK_AUDIO_BINS - i) / (float)MILK_AUDIO_BINS);
        cosine[i] = cosf(2 * PI * i / FFT_SIZE);
        sine[i]   = -sinf(2 * PI * i / FFT_SIZE);
    }

    for (unsigned i = 1, j = 0; i < FFT_SIZE; ++i)
    {
        unsigned bit = FFT_SIZE >> 1;

        for (; j & bit; bit >>= 1)
        {
            j ^= bit;
        }

        j ^= bit;

        if (i < j)
        {
            fft_swaps[fft_swap_count][0] = (uint16_t)i;
            fft_swaps[fft_swap_count][1] = (uint16_t)j;

            ++fft_swap_count;
        }
    }

    unsigned offset = 0, weight_offset = 0;

    for (unsigned level = 0; level < ALIGNMENT_LEVELS; ++level)
    {
        unsigned count = (unsigned)ALIGNMENT_SAMPLES >> level;

        alignment_offset[level]        = offset;
        alignment_weight_offset[level] = weight_offset;
        offset += (unsigned)MILK_AUDIO_SAMPLES >> level;
        weight_offset += count;

        for (unsigned i = 0; i < count; ++i)
        {
            float w = (i < count / 2 ? i : count - 1 - i) * 2.0f / count;

            alignment_weight[alignment_weight_offset[level] + i] = fminf(1, fmaxf(0, (w - .8f) * 5 + .8f));
        }
    }

    initialized = 1;
}

/**
 * @brief Compute the windowed, equalized reference spectrum.
 *
 * @param samples MILK_AUDIO_SAMPLES input amplitudes.
 * @param output Destination of MILK_AUDIO_BINS magnitudes.
 */
static void spectrum(const float samples[MILK_AUDIO_SAMPLES], float output[MILK_AUDIO_BINS])
{
    float re[FFT_SIZE] = { 0 }, im[FFT_SIZE];

    for (unsigned i = 0; i < MILK_AUDIO_SAMPLES; ++i)
    {
        re[i] = samples[i] * window[i];
    }

    for (unsigned swap = 0; swap < fft_swap_count; ++swap)
    {
        unsigned i = fft_swaps[swap][0], j = fft_swaps[swap][1];
        float    temp = re[i];

        re[i] = re[j];
        re[j] = temp;
    }

    fft_real_start(re, im, FFT_SIZE, cosine[FFT_SIZE / 4], sine[FFT_SIZE / 4]);

    for (unsigned size = 8; size <= FFT_SIZE; size <<= 1)
    {
        unsigned step = FFT_SIZE / size;

        for (unsigned base = 0; base < FFT_SIZE; base += size)
        {
            /* The first twiddle is exactly (1, -0): no complex multiply.
             * Keep each sum/difference and all later butterflies in order. */
            unsigned other    = base + size / 2;
            float    first_re = re[base], first_im = im[base];
            float    other_re = re[other], other_im = im[other];

            re[other] = first_re - other_re;
            im[other] = first_im - other_im;
            re[base]  = first_re + other_re;
            im[base]  = first_im + other_im;

            for (unsigned j = 1; j < size / 2; ++j)
            {
                unsigned p = base + j, q = p + size / 2, k = j * step;
                float    r = cosine[k] * re[q] - sine[k] * im[q];
                float    v = sine[k] * re[q] + cosine[k] * im[q];

                re[q] = re[p] - r;
                im[q] = im[p] - v;
                re[p] += r;
                im[p] += v;
            }
        }
    }

    for (unsigned k = 0; k < MILK_AUDIO_BINS; ++k)
    {
        output[k] = weight[k] * sqrtf(re[k] * re[k] + im[k] * im[k]);
    }
}

/**
 * @brief Align one waveform to the previous frame using coarse-to-fine matching.
 *
 * Shared scratch buffers avoid large per-channel stack allocations on the EE.
 *
 * @param s Waveform and alignment history to update.
 * @param ch Channel index, 0 for left or 1 for right.
 */
static void align_wave(MilkAudio* s, unsigned ch)
{
    static float current[ALIGNMENT_STORAGE(MILK_AUDIO_SAMPLES)], previous[ALIGNMENT_STORAGE(MILK_AUDIO_SAMPLES)];

    memcpy(current, s->waveform[ch], sizeof(s->waveform[ch]));
    memcpy(previous, s->previous[ch] + s->offsets[ch], ALIGNMENT_SAMPLES * sizeof(float));
    /* All coarser levels are overwritten below; only the base padding needs clearing. */
    memset(previous + ALIGNMENT_SAMPLES, 0, ALIGNMENT_MAX_SHIFT * sizeof(float));

    for (unsigned level = 1; level < ALIGNMENT_LEVELS; ++level)
    {
        unsigned base = alignment_offset[level], parent = alignment_offset[level - 1];

        for (unsigned i = 0; i < ((unsigned)MILK_AUDIO_SAMPLES >> level); ++i)
        {
            current[base + i]  = .5f * (current[parent + 2 * i] + current[parent + 2 * i + 1]);
            previous[base + i] = .5f * (previous[parent + 2 * i] + previous[parent + 2 * i + 1]);
        }
    }

    int lo = 0, hi = ALIGNMENT_INITIAL_CANDIDATES, best = 0;

    for (int level = ALIGNMENT_LEVELS - 1; level >= 0; --level)
    {
        unsigned base   = alignment_offset[level];
        int      count  = (MILK_AUDIO_SAMPLES >> level) - (ALIGNMENT_MAX_SHIFT >> level);
        float    lowest = 0;

        best = -1;

        for (int offset = lo; offset < hi; ++offset)
        {
            float error = 0;

            for (int i = 0; i < count; ++i)
            {
                float w = alignment_weight[alignment_weight_offset[level] + i];

                error += fabsf((current[base + i + offset] - previous[base + i]) * w);
            }

            if (best < 0 || error < lowest)
            {
                best   = offset;
                lowest = error;
            }
        }

        if (level)
        {
            lo = best * 2 - 1;
            hi = best * 2 + 3;

            if (lo < 0)
            {
                lo = 0;
            }

            if (hi > (ALIGNMENT_MAX_SHIFT >> (level - 1)))
            {
                hi = ALIGNMENT_MAX_SHIFT >> (level - 1);
            }
        }
    }

    memcpy(s->previous[ch], s->waveform[ch], sizeof(s->previous[ch]));

    s->offsets[ch] = (unsigned)best;

    if (best > 0)
    {
        memmove(s->waveform[ch], s->waveform[ch] + best, ALIGNMENT_SAMPLES * sizeof(float));
        memset(s->waveform[ch] + ALIGNMENT_SAMPLES, 0, ALIGNMENT_MAX_SHIFT * sizeof(float));
    }
}

void milk_audio_step(MilkAudio* state, const Audio* audio, float seconds, int custom_spectrum)
{
    if (!isfinite(seconds) || seconds <= 0)
    {
        return;
    }

    initialize();

    for (unsigned ch = 0; ch < 2; ++ch)
    {
        memset(state->waveform[ch], 0, sizeof(state->waveform[ch]));

        if (audio->active && audio->history_count >= INPUT_HISTORY_REQUIRED)
        {
            for (unsigned i = 0; i < MILK_AUDIO_SAMPLES; ++i)
            {
                float    back     = (MILK_AUDIO_SAMPLES - 1 - i) * (AUDIO_RATE / REFERENCE_AUDIO_RATE);
                unsigned offset   = (unsigned)back;
                float    fraction = back - offset;
                unsigned newer    = (audio->history_write + AUDIO_HISTORY - 1 - offset) % AUDIO_HISTORY;
                unsigned older    = (newer + AUDIO_HISTORY - 1) % AUDIO_HISTORY;

                state->waveform[ch][i] = (audio->history[newer][ch] + fraction * (audio->history[older][ch] - audio->history[newer][ch])) / REFERENCE_PCM_SCALE;
            }
        }

        if (custom_spectrum)
        {
            /* Shell spectrum precedes alignment and uses two-sample damping. */
            float damped[MILK_AUDIO_SAMPLES];

            damped[0] = state->waveform[ch][0];

            for (unsigned i = 1; i < MILK_AUDIO_SAMPLES; ++i)
            {
                damped[i] = .5f * (state->waveform[ch][i] + state->waveform[ch][i - 1]);
            }

            spectrum(damped, state->spectrum[ch]);
        }

        align_wave(state, ch);
    }

    spectrum(state->waveform[0], state->left_spectrum);
    /* Match CPlugin::DoCustomSoundAnalysis, including the enabled equalizer
     * in myfft.Init(576, 512, -1): -1 is truthy, NOT 'disable equalization'. */
    float slow = powf(state->frames < 50 ? .9f : .992f, 30 * seconds);

    for (unsigned band = 0; band < 3; ++band)
    {
        float value = 0;

        for (unsigned k = MILK_AUDIO_BINS * band / 6; k < MILK_AUDIO_BINS * (band + 1) / 6; ++k)
        {
            value += state->left_spectrum[k];
        }

        state->immediate[band] = value;

        float rate = powf(value > state->average[band] ? .2f : .5f, 30 * seconds);

        state->average[band]      = state->average[band] * rate + value * (1 - rate);
        state->long_average[band] = state->long_average[band] * slow + value * (1 - slow);
        state->relative[band]     = fabsf(state->long_average[band]) < .001f ? 1 : value / state->long_average[band];
        state->attenuated[band]   = fabsf(state->long_average[band]) < .001f ? 1 : state->average[band] / state->long_average[band];
    }

    if (state->frames < 50)
    {
        ++state->frames;
    }
}
