/* Classic DrawCustomShapes/DrawCustomWaves adapted to bounded PS2 storage. */
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

#define OBJECT_SEED_MULTIPLIER 0x9e3779b9u

/**
 * @brief Copy shared audio/time inputs and q variables into an object variable pool.
 *
 * @param p Parent preset.
 * @param v Destination object variable array.
 */
static void inputs(const Preset* p, float* v)
{
    memcpy(v + MO_TIME, p->milk.inputs, sizeof(p->milk.inputs));
    memcpy(v + MO_Q1, p->milk.frame + ML_Q1, MILK_Q_VARIABLES * sizeof(float));
}

void milk_objects_init(Preset* p)
{
    const MilkProgram* program = &milk_programs[(unsigned)p->kind];

    for (unsigned i = 0; i < program->object_count; ++i)
    {
        const MilkObjectProgram* d = &program->objects[i];
        MilkObjectState*         s = &p->milk.objects[i];

        s->random = p->seed ^ (OBJECT_SEED_MULTIPLIER * (i + 1));

        if (!s->random)
        {
            s->random = 1;
        }

        memcpy(s->frame, d->defaults, sizeof(d->defaults));
        inputs(p, s->frame);
        d->init(s->frame, &s->random);
        memcpy(s->initial_t, s->frame + MO_T1, sizeof(s->initial_t));
    }
}

/**
 * @brief Restore object defaults, shared inputs, and initialized t variables.
 *
 * @param p Parent preset.
 * @param d Compiled object program.
 * @param s Object state to update.
 */
static void reset_frame(const Preset* p, const MilkObjectProgram* d, MilkObjectState* s)
{
    memcpy(s->frame, d->defaults, sizeof(d->defaults));
    inputs(p, s->frame);
    memcpy(s->frame + MO_T1, s->initial_t, sizeof(s->initial_t));
}

/**
 * @brief Evaluate a custom wave and cache its per-point geometry.
 *
 * @param p Parent preset.
 * @param audio Waveform and spectrum inputs.
 * @param d Compiled wave program.
 * @param s Object state and geometry cache to update.
 */
static void wave_step(const Preset* p, const MusicFeatures* audio, const MilkObjectProgram* d, MilkObjectState* s)
{
    reset_frame(p, d, s);
    memcpy(s->point + MO_TIME, s->frame + MO_TIME, MILK_FRAME_INPUTS * sizeof(float));
    PROFILE_BEGIN(frame_equations_begin);
    d->frame(s->frame, &s->random);
    PROFILE_END(wave_frame_equations, frame_equations_begin);

    float*   f     = s->frame;
    unsigned count = (unsigned)milk_bound(f[MO_SAMPLES], 0, MILK_OBJECT_SAMPLES);

    if (count < (d->defaults[MO_DOTS] ? 1u : 2u))
    {
        s->count = 0;

        return;
    }

    PROFILE_BEGIN(samples_begin);

    float data[2][MILK_OBJECT_SAMPLES];
    int   spectrum  = d->defaults[MO_SPECTRUM] != 0;
    int   sep       = (int)milk_bound(d->defaults[MO_SEP], -(MILK_AUDIO_BINS - 1), MILK_AUDIO_BINS - 1);
    float smoothing = sqrtf(milk_bound(d->defaults[MO_SMOOTHING], 0, 1) * .98f);
    float scale     = d->defaults[MO_SCALING] * milk_programs[(unsigned)p->kind].defaults[ML_WAVE_SCALE] * (spectrum ? .15f : .512f);

    for (unsigned ch = 0; ch < 2; ++ch)
    {
        for (unsigned j = 0; j < count; ++j)
        {
            int      position = spectrum ? (int)(j * ((float)MILK_AUDIO_BINS - sep) / count) : (480 - (int)count) / 2 + (ch ? sep / 2 : -sep / 2) + (int)j;
            unsigned index    = (unsigned)milk_bound(position, 0, spectrum ? MILK_AUDIO_BINS - 1 : MILK_AUDIO_SAMPLES - 1);
            float    sample   = spectrum ? audio->milk_audio.spectrum[ch][index] : audio->custom_wave[ch][index];

            data[ch][j] = j ? sample * (1 - smoothing) + data[ch][j - 1] * smoothing : sample;
        }

        for (int j = (int)count - 2; j >= 0; --j)
        {
            data[ch][j] = data[ch][j] * (1 - smoothing) + data[ch][j + 1] * smoothing;
        }

        for (unsigned j = 0; j < count; ++j)
        {
            data[ch][j] = milk_finite(data[ch][j] * scale);
        }
    }

    PROFILE_END(wave_samples, samples_begin);
    d->points(s, data, count);

    s->count = count;
}

void milk_objects_step(Preset* p, const MusicFeatures* audio)
{
    const MilkProgram* program = &milk_programs[(unsigned)p->kind];

    for (unsigned i = 0; i < program->object_count; ++i)
    {
        const MilkObjectProgram* d = &program->objects[i];
        MilkObjectState*         s = &p->milk.objects[i];

        if (d->type)
        {
            PROFILE_BEGIN(wave_begin);
            wave_step(p, audio, d, s);
            PROFILE_END(custom_wave_update, wave_begin);
            continue;
        }

        PROFILE_BEGIN(shape_begin);

        s->count = (unsigned)milk_bound(d->defaults[MO_INSTANCES], 1, MILK_OBJECT_INSTANCES);

        for (unsigned instance = 0; instance < s->count; ++instance)
        {
            reset_frame(p, d, s);

            s->frame[MO_INSTANCE] = instance;

            d->frame(s->frame, &s->random);
            memcpy(s->cache.shape[instance], s->frame, sizeof(s->cache.shape[instance]));
        }

        PROFILE_END(custom_shape_update, shape_begin);
    }
}

/** @brief Values shared by every perimeter vertex of one shape instance. */
typedef struct
{
    MilkVertex rim;
    float      radius;
    float      zoom;
    float      angle;
    float      tex_angle;
    unsigned   sides;
    int        textured;
} ShapeGeometry;

/**
 * @brief Construct a perimeter vertex using prepared shape-wide values.
 * @param shape Prepared shape geometry.
 * @param index Perimeter vertex index.
 * @return Pixel-space vertex with color and texture coordinates.
 */
static MilkVertex shape_vertex(const ShapeGeometry* shape, unsigned index)
{
    /* Keep multiply/divide and angle-addition order identical to the scalar path. */
    float      t     = PRESET_TAU * index / shape->sides;
    float      angle = t + shape->angle + PRESET_TAU / 8;
    MilkVertex v     = shape->rim;

    v.x += shape->radius * cosf(angle);
    v.y -= shape->radius * sinf(angle);

    if (shape->textured)
    {
        float tex_angle = t + shape->tex_angle + PRESET_TAU / 8;

        v.u = .5f + (.5f * MILK_VIEWPORT_ASPECT) * cosf(tex_angle) / shape->zoom;
        v.v = .5f + .5f * sinf(tex_angle) / shape->zoom;
    }

    return v;
}

/**
 * @brief Draw the fill and outline of one custom shape instance.
 *
 * @param f Evaluated shape fields.
 * @param c Drawing sink.
 * @param wrap Nonzero to wrap feedback texture coordinates.
 */
static void shape_draw(const float* f, PresetCanvas* c, int wrap)
{
    unsigned sides = (unsigned)milk_bound(f[MO_SIDES], 3, 100);

    preset_blend(c, f[MO_ADDITIVE] != 0);

    MilkVertex    center = { milk_bound(f[MO_X], -4, 5) * DISPLAY_WIDTH, milk_bound(f[MO_Y], -4, 5) * DISPLAY_HEIGHT, milk_bound(f[MO_R], 0, 1) * 255, milk_bound(f[MO_G], 0, 1) * 255, milk_bound(f[MO_B], 0, 1) * 255, milk_bound(f[MO_A], 0, 1), .5f, .5f };
    ShapeGeometry shape  = {
        .rim       = { center.x, center.y, milk_bound(f[MO_R2], 0, 1) * 255, milk_bound(f[MO_G2], 0, 1) * 255, milk_bound(f[MO_B2], 0, 1) * 255, milk_bound(f[MO_A2], 0, 1), 0, 0 },
        .radius    = milk_bound(f[MO_RAD], -4, 4) * MILK_VIEWPORT_HALF_HEIGHT,
        .zoom      = milk_bound(fabsf(f[MO_TEX_ZOOM]), .2f, 100),
        .angle     = f[MO_ANG],
        .tex_angle = f[MO_TEX_ANG],
        .sides     = sides,
        .textured  = f[MO_TEXTURED] != 0,
    };

    if (f[MO_TEX_ZOOM] < 0)
    {
        shape.zoom = -shape.zoom;
    }

    int        border  = f[MO_BORDER_A] > 0;
    MilkVertex outline = { 0 };

    if (border)
    {
        outline.r = milk_bound(f[MO_BORDER_R], 0, 1) * 255;
        outline.g = milk_bound(f[MO_BORDER_G], 0, 1) * 255;
        outline.b = milk_bound(f[MO_BORDER_B], 0, 1) * 255;
        outline.a = milk_bound(f[MO_BORDER_A], 0, 1);
    }

    unsigned   copies   = f[MO_THICK] != 0 ? 4 : 1;
    MilkVertex previous = shape_vertex(&shape, 0);

    for (unsigned j = 0; j < sides; ++j)
    {
        MilkVertex a = previous, b = shape_vertex(&shape, j + 1);

        /* Save fill attributes before the outline overwrites colors/alpha. */
        previous = b;

        MilkVertex triangle[3] = { center, a, b };

        if (center.a > 0 || a.a > 0)
        {
            milk_object_triangle(c, triangle, shape.textured, wrap);
        }

        if (border)
        {
            a.r = b.r = outline.r;
            a.g = b.g = outline.g;
            a.b = b.b = outline.b;
            a.a = b.a = outline.a;

            for (unsigned k = 0; k < copies; ++k)
            {
                MilkVertex u = a, v = b;

                u.x += k == 1 || k == 2;
                v.x += k == 1 || k == 2;
                u.y += k >= 2;
                v.y += k >= 2;

                milk_object_line(c, u, v);
            }
        }
    }
}

void milk_objects_draw(const Preset* p, PresetCanvas* canvas)
{
    if (canvas->opacity <= 0)
    {
        return;
    }

    const MilkProgram* program = &milk_programs[(unsigned)p->kind];

    for (unsigned i = 0; i < program->object_count; ++i)
    {
        const MilkObjectProgram* d = &program->objects[i];
        const MilkObjectState*   s = &p->milk.objects[i];

        if (!d->type)
        {
            PROFILE_DRAW_BEGIN(shape_begin);

            for (unsigned j = 0; j < s->count; ++j)
            {
                shape_draw(s->cache.shape[j], canvas, p->milk.frame[ML_WRAP] != 0);
            }

            PROFILE_DRAW_END(shape_geometry, shape_begin);
            continue;
        }

        PROFILE_DRAW_BEGIN(wave_begin);
        preset_blend(canvas, d->defaults[MO_ADDITIVE] != 0);

        for (unsigned j = 0; j < s->count; ++j)
        {
            MilkVertex v = s->cache.wave[j];

            if (d->defaults[MO_DOTS])
            {
                if (canvas->sprite && v.x >= 0 && v.x <= DISPLAY_WIDTH && v.y >= 0 && v.y <= DISPLAY_HEIGHT)
                {
                    PresetVertex dot = { v.x, v.y, v.r, v.g, v.b };

                    canvas->sprite(canvas->context, dot, d->defaults[MO_THICK] ? 1.5f : .7f, v.a * canvas->opacity);
                }

                continue;
            }

            if (!j)
            {
                continue;
            }

            MilkVertex a = s->cache.wave[j - 1], b = v;

            // The reference's four-tap midpoint inserts detail without more equations.
            MilkVertex before = s->cache.wave[j > 1 ? j - 2 : 0], after = s->cache.wave[j + 1 < s->count ? j + 1 : j], mid = a;

            mid.x = (-.15f * before.x + 1.15f * a.x + 1.15f * b.x - .15f * after.x) * .5f;
            mid.y = (-.15f * before.y + 1.15f * a.y + 1.15f * b.y - .15f * after.y) * .5f;

            unsigned copies = d->defaults[MO_THICK] ? 4 : 1;

            /* Both smoothed segments and every thickness offset are outside
             * the same screen edge. Keep a one-pixel margin for thick waves;
             * boundary-touching segments retain the original clipping path. */
            float margin = copies == 4 ? 1.0f : 0.0f;

            if ((a.x < -margin && mid.x < -margin && b.x < -margin) ||
                (a.x > DISPLAY_WIDTH && mid.x > DISPLAY_WIDTH && b.x > DISPLAY_WIDTH) ||
                (a.y < -margin && mid.y < -margin && b.y < -margin) ||
                (a.y > DISPLAY_HEIGHT && mid.y > DISPLAY_HEIGHT && b.y > DISPLAY_HEIGHT))
            {
                continue;
            }

            milk_object_wave_segment(canvas, a, mid, b, copies);
        }

        PROFILE_DRAW_END(wave_geometry, wave_begin);
    }
}
