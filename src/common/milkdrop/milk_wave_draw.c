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

#include "milkdrop/preset.h"
#include "profiling/benchmark.h"
#include "milkdrop/viewport.h"
#include <math.h>
#include <string.h>

/**
 * @brief Classify a vertex against the shared viewport.
 *
 * @param p Pixel-space vertex.
 * @return Outside-edge bitmask; zero means inside.
 */
static int outcode(PresetVertex p)
{
    return (p.x < 0 ? 1 : 0) | (p.x > DISPLAY_WIDTH ? 2 : 0) | (p.y < 0 ? 4 : 0) | (p.y > DISPLAY_HEIGHT ? 8 : 0);
}

/**
 * @brief Clip a line to the viewport before emitting it.
 *
 * @param c Drawing sink.
 * @param a Start vertex.
 * @param b End vertex.
 * @param ink Opacity multiplier.
 */
static void clipped_line(PresetCanvas* c, PresetVertex a, PresetVertex b, float ink)
{
    for (unsigned n = 0; n < 8; ++n)
    {
        int ca = outcode(a), cb = outcode(b);

        if (ca & cb)
        {
            return;
        }

        if (!(ca | cb))
        {
            preset_line(c, a, b, ink);
            return;
        }

        int   code = ca ? ca : cb;
        float t;

        if (code & 12)
        {
            t = ((code & 4 ? 0 : DISPLAY_HEIGHT) - a.y) / (b.y - a.y);
        }
        else
        {
            t = ((code & 1 ? 0 : DISPLAY_WIDTH) - a.x) / (b.x - a.x);
        }

        PresetVertex p = a;

        p.x += t * (b.x - a.x);
        p.y += t * (b.y - a.y);

        if (ca)
        {
            a = p;
        }
        else
        {
            b = p;
        }
    }
}

/**
 * @brief Sanitize and clamp a value to 0..1.
 *
 * @param x Input value.
 * @return Finite unit-range value.
 */
static float unit(float x)
{
    return fminf(1, fmaxf(0, milk_finite(x)));
}

void milk_wave_style(const Preset* p, const MusicFeatures* audio, MilkWaveStyle* style)
{
    const float* v = p->milk.frame;

    style->r = unit(v[ML_WAVE_R]);
    style->g = unit(v[ML_WAVE_G]);
    style->b = unit(v[ML_WAVE_B]);

    float peak = fmaxf(style->r, fmaxf(style->g, style->b));

    if (v[ML_WAVE_BRIGHTEN] && peak > .01f)
    {
        style->r /= peak;
        style->g /= peak;
        style->b /= peak;
    }

    unsigned mode  = (unsigned)fminf(8, fmaxf(0, v[ML_WAVE_MODE]));
    float    alpha = v[ML_WAVE_A];

    if (mode == 1)
    {
        alpha *= 1.25f;
    }

    if (mode == 2 || mode == 5)
    {
        alpha *= .09f; /* nearest reference texture tier: 512 (effect tuning, not viewport height) */
    }

    if (mode == 3)
    {
        alpha = .15f * 1.3f * audio->milk_audio.immediate[2] * audio->milk_audio.immediate[2];
    }

    if (v[ML_WAVE_MOD_ALPHA])
    {
        alpha *= milk_div((audio->relative[0] + audio->relative[1] + audio->relative[2]) * .333f - v[ML_WAVE_MOD_START], v[ML_WAVE_MOD_END] - v[ML_WAVE_MOD_START]);
    }

    style->alpha    = unit(alpha);
    style->dots     = v[ML_WAVE_DOTS] != 0;
    style->thick    = v[ML_WAVE_THICK] != 0;
    style->additive = v[ML_WAVE_ADDITIVE] != 0;
}

void milk_wave_morph(const MilkWave* old, const MilkWave* next, float mix, MilkWave* out)
{
    if (mix <= 0)
    {
        *out = *old;

        return;
    }

    if (mix >= 1)
    {
        *out = *next;

        return;
    }

    *out        = *next;
    out->closed = 0;

    if (!old->count || !next->count)
    {
        return;
    }

    /* Resample the old shape onto the incoming shape, as DrawWave does.
     * Carry both discontinuities so dual stereo curves never gain a bridge. */
    float ratio = (old->count - 1) / (float)next->count;

    for (unsigned i = 0; i < next->count; ++i)
    {
        float     f = i * ratio;
        unsigned  j = (unsigned)f;
        float     t = f - j;
        MilkPoint p = old->point[j];

        if (j + 1 == old->break_at || j + 1 == old->break_at2)
        {
            out->break_at2 = i + 1;
        }
        else if (j + 1 < old->count)
        {
            p.x += (old->point[j + 1].x - p.x) * t;
            p.y += (old->point[j + 1].y - p.y) * t;
        }

        out->point[i].x = p.x + (next->point[i].x - p.x) * mix;
        out->point[i].y = p.y + (next->point[i].y - p.y) * mix;
    }
}

/**
 * @brief Build a smoothed waveform and explicitly close any loop.
 *
 * @param p MilkDrop preset.
 * @param a Audio features.
 * @param shape Destination waveform geometry.
 */
static void prepare(const Preset* p, const MusicFeatures* a, MilkWave* shape)
{
    float samples[2][MILK_AUDIO_SAMPLES];

    milk_smooth_wave(a, p->milk.frame[ML_WAVE_SCALE], p->milk.frame[ML_WAVE_SMOOTHING], samples);
    milk_wave_shape(p, a, samples, shape);

    if (shape->closed && shape->count < MILK_WAVE_MAX)
    {
        shape->point[shape->count++] = shape->point[0];
        shape->closed                = 0;
    }
}

/**
 * @brief Draw a waveform dot or connected segment and update continuity.
 *
 * @param c Drawing sink.
 * @param p Current pixel-space point.
 * @param s Waveform appearance.
 * @param previous Previous vertex, updated on valid input.
 * @param connected Continuity flag, cleared on non-finite coordinates.
 */
static void emit_point(PresetCanvas* c, MilkPoint p, const MilkWaveStyle* s, PresetVertex* previous, int* connected)
{
    PresetVertex v = { p.x, p.y, 255 * s->r, 255 * s->g, 255 * s->b };

    if (!isfinite(v.x) || !isfinite(v.y))
    {
        *connected = 0;

        return;
    }

    if (s->dots)
    {
        if (!outcode(v))
        {
            c->sprite(c->context, v, .5f, c->opacity * s->alpha);
        }
    }
    else if (*connected)
    {
        clipped_line(c, *previous, v, s->alpha);
    }

    *previous  = v;
    *connected = 1;
}

void milk_wave_draw(const Preset* old, const Preset* next, float mix, PresetCanvas* c, const MusicFeatures* audio)
{
    PROFILE_DRAW_BEGIN(wave_begin);

    MilkWave      first, second, result;
    MilkWaveStyle a, b;

    prepare(old, audio, &first);
    milk_wave_style(old, audio, &a);

    if (next)
    {
        prepare(next, audio, &second);
        milk_wave_style(next, audio, &b);
        milk_wave_morph(&first, &second, mix, &result);

        a.r += (b.r - a.r) * mix;
        a.g += (b.g - a.g) * mix;
        a.b += (b.b - a.b) * mix;
        a.alpha += (b.alpha - a.alpha) * mix;

        if (mix >= .5f)
        {
            a.dots     = b.dots;
            a.thick    = b.thick;
            a.additive = b.additive;
        }
    }
    else
    {
        result = first;
    }

    if (a.alpha < .004f || c->opacity <= 0)
    {
        PROFILE_DRAW_END(wave_geometry, wave_begin);
        return;
    }

    preset_blend(c, a.additive);
    /* Midpoints are identical in every thickness pass. Preserve the exact
     * arithmetic and apply each pixel offset only when emitting the point. */
    MilkPoint midpoint[MILK_WAVE_MAX];

    for (unsigned begin = 0; begin < result.count;)
    {
        unsigned end = result.count;

        if (result.break_at > begin && result.break_at < end)
        {
            end = result.break_at;
        }

        if (result.break_at2 > begin && result.break_at2 < end)
        {
            end = result.break_at2;
        }

        for (unsigned i = begin; i + 1 < end; ++i)
        {
            MilkPoint lo = result.point[i > begin ? i - 1 : i], hi = result.point[i + 1], hh = result.point[i + 2 < end ? i + 2 : end - 1];

            midpoint[i] = (MilkPoint){ (-.15f * lo.x + 1.15f * result.point[i].x + 1.15f * hi.x - .15f * hh.x) * .5f, (-.15f * lo.y + 1.15f * result.point[i].y + 1.15f * hi.y - .15f * hh.y) * .5f };
        }

        begin = end;
    }

    unsigned    passes        = (a.thick || a.dots) ? 4 : 1;
    const float offsets[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

    for (unsigned pass = 0; pass < passes; ++pass)
    {
        unsigned begin = 0;

        while (begin < result.count)
        {
            unsigned end = result.count;

            if (result.break_at > begin && result.break_at < end)
            {
                end = result.break_at;
            }

            if (result.break_at2 > begin && result.break_at2 < end)
            {
                end = result.break_at2;
            }

            PresetVertex previous  = { 0 };
            int          connected = 0;

            for (unsigned i = begin; i < end; ++i)
            {
                MilkPoint p = result.point[i];

                p.x += offsets[pass][0];
                p.y += offsets[pass][1];

                emit_point(c, p, &a, &previous, &connected);

                if (i + 1 < end)
                {
                    MilkPoint mid = midpoint[i];

                    mid.x += offsets[pass][0];
                    mid.y += offsets[pass][1];

                    emit_point(c, mid, &a, &previous, &connected);
                }
            }

            begin = end;
        }
    }

    preset_blend(c, 0);
    PROFILE_DRAW_END(wave_geometry, wave_begin);
}
