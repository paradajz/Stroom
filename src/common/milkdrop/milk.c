/* Classic waveform equations adapted from MilkDrop DrawWave; float32 PS2 runtime. */
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

#include "milkdrop/milk.h"
#include "profiling/benchmark.h"
#include "milkdrop/viewport.h"
#include "util/random.h"
#include "milkdrop/preset.h"
#include "milkdrop/feedback.h"
#include <math.h>
#include <string.h>

/* Largest float32 integers safely convertible to signed equation operands. */
#define EQUATION_INT64_MAX    9223371487098961920.0f
#define EQUATION_INT64_MIN    (-9223372036854775808.0f)
#define SHADE_SEED_MULTIPLIER 1664525u
#define SHADE_SEED_INCREMENT  1013904223u

/**
 * @brief Convert an equation operand to a bounded signed 64-bit integer.
 *
 * @param x Value; non-finite input becomes zero.
 * @return Truncated and bounded integer.
 */
static int64_t bit_integer(float x)
{
    /* EEL bitwise operands truncate to signed 64-bit integers. */
    return (int64_t)fminf(EQUATION_INT64_MAX, fmaxf(EQUATION_INT64_MIN, milk_finite(x)));
}

float milk_bitand(float a, float b)
{
    return (float)(bit_integer(a) & bit_integer(b));
}

float milk_bitor(float a, float b)
{
    return (float)(bit_integer(a) | bit_integer(b));
}

float milk_pow(float a, float b)
{
    /* Sonic and other custom waves use x^2.5 for opacity. Keep all
     * intermediates normal and finite; other domains retain libm semantics.
     * This changes only float rounding, not the mathematical curve. */

    if (b == 2.5f && a >= 1e-6f && a <= 1e6f)
    {
        return (a * a) * sqrtf(a);
    }

    return milk_finite(powf(a, b));
}

float milk_sqrt(float x)
{
    return sqrtf(fabsf(x));
}

float milk_sqr(float x)
{
    return milk_finite(x * x);
}

float milk_asin(float x)
{
    return asinf(fminf(1, fmaxf(-1, x)));
}

float milk_acos(float x)
{
    return acosf(fminf(1, fmaxf(-1, x)));
}

float milk_exp(float x)
{
    return milk_finite(expf(x));
}

float milk_log(float x)
{
    return milk_finite(logf(x));
}

float milk_log10(float x)
{
    return milk_finite(log10f(x));
}

float milk_sigmoid(float a, float b)
{
    return 1 / (1 + expf(-a * b));
}

float milk_rand(uint32_t* state, float range)
{
    uint32_t x = util_random_next_u32(*state);

    *state      = x;
    float count = fmaxf(1, truncf(range));

    return floorf((x >> 8) * (1.0f / 16777216) * count);
}

/**
 * @brief Look up the compiled equations for a MilkDrop preset.
 *
 * @param p Preset whose kind identifies a MilkDrop library entry.
 * @return Borrowed compiled program.
 */
static const MilkProgram* program(const Preset* p)
{
    return &milk_programs[(unsigned)p->kind];
}

void milk_init_at(Preset* p, float time, unsigned frame)
{
    MilkState*         s = &p->milk;
    const MilkProgram* d = program(p);

    memset(s, 0, sizeof(*s));

    s->random = p->seed ? p->seed : 1;

    memcpy(s->frame, d->defaults, sizeof(d->defaults));

    s->frame[ML_TIME]  = time;
    s->frame[ML_FRAME] = (float)frame;

    memcpy(s->inputs, s->frame + ML_TIME, sizeof(s->inputs));
    d->init(s->frame, &s->random);
    memcpy(s->initial_q, s->frame + ML_Q1, sizeof(s->initial_q));
    milk_objects_init(p);
}

void milk_step(Preset* p, const MusicFeatures* a, float dt)
{
    PROFILE_BEGIN(equations_begin);

    MilkState*         s = &p->milk;
    const MilkProgram* d = program(p);
    float*             v = s->frame;

    memcpy(v, d->defaults, sizeof(d->defaults));
    memcpy(v + ML_Q1, s->initial_q, sizeof(s->initial_q));

    v[ML_TIME]     = a->time;
    v[ML_FRAME]    = (float)a->frame;
    v[ML_FPS]      = 1 / dt;
    v[ML_BASS]     = a->relative[0];
    v[ML_MID]      = a->relative[1];
    v[ML_TREB]     = a->relative[2];
    v[ML_BASS_ATT] = a->attenuated[0];
    v[ML_MID_ATT]  = a->attenuated[1];
    v[ML_TREB_ATT] = a->attenuated[2];

    memcpy(s->inputs, v + ML_TIME, sizeof(s->inputs));
    d->frame(v, &s->random);
    PROFILE_END(preset_equations, equations_begin);
    milk_objects_step(p, a);
}

void milk_begin_vertices(Preset* p)
{
    /* Read-only frame inputs and q transfer happen once, before vertex code.
     * Custom vertex variables survive vertices AND frames, like the reference. */
    memcpy(p->milk.vertex + ML_TIME, p->milk.frame + ML_TIME, (MILK_BUILTIN_COUNT - ML_TIME) * sizeof(float));
    memcpy(p->milk.vertex + ML_TIME, p->milk.inputs, sizeof(p->milk.inputs));
}

/**
 * @brief Translate equation variables into bounded feedback settings.
 *
 * @param p Preset supplying the shared clock.
 * @param v Equation variable array.
 * @param f Destination transform.
 */
static void values_transform(const Preset* p, const float* v, FeedbackTransform* f)
{
    memset(f, 0, sizeof(*f));

    f->zoom     = fmaxf(.001f, v[ML_ZOOM]);
    f->zoom_exp = fminf(8, fmaxf(.01f, v[ML_ZOOMEXP]));
    f->rotation = v[ML_ROT];
    f->warp     = v[ML_WARP];
    f->cx       = v[ML_CX];
    f->cy       = v[ML_CY];
    f->dx       = v[ML_DX];
    f->dy       = v[ML_DY];
    f->sx       = v[ML_SX];
    f->sy       = v[ML_SY];

    if (fabsf(f->sx) < .001f)
    {
        f->sx = .001f;
    }

    if (fabsf(f->sy) < .001f)
    {
        f->sy = .001f;
    }

    f->warp_time  = p->milk.inputs[0] * v[ML_WARP_SPEED];
    f->warp_scale = fmaxf(.001f, v[ML_WARP_SCALE]);
    f->decay      = fminf(1, fmaxf(0, v[ML_DECAY]));
    f->wrap       = v[ML_WRAP] != 0;
}

void milk_frame_transform(const Preset* p, FeedbackTransform* f)
{
    values_transform(p, p->milk.frame, f);
}

void milk_transform(Preset* p, const MilkVertexInput* input, FeedbackTransform* f)
{
    float* v = p->milk.vertex;

    memcpy(v, p->milk.frame, MILK_OUTPUT_COUNT * sizeof(float));

    v[ML_X]   = input->x;
    v[ML_Y]   = input->y;
    v[ML_RAD] = input->radius;
    v[ML_ANG] = input->angle;

    program(p)->vertex(v, &p->milk.random);
    values_transform(p, v, f);
}

/**
 * @brief Build a pixel-space vertex using three equation color channels.
 *
 * @param x Horizontal pixel coordinate.
 * @param y Vertical pixel coordinate.
 * @param v Equation variable array.
 * @param rgb Index of the first RGB variable.
 * @return Vertex with clamped RGB channels scaled to 0..255.
 */
static PresetVertex vertex(float x, float y, const float* v, int rgb)
{
    return (PresetVertex){ x, y, 255 * fminf(1, fmaxf(0, v[rgb])), 255 * fminf(1, fmaxf(0, v[rgb + 1])), 255 * fminf(1, fmaxf(0, v[rgb + 2])) };
}

/**
 * @brief Draw one rectangular preset border.
 *
 * @param c Drawing sink.
 * @param v Equation variable array.
 * @param base Index of the size/color/alpha field group.
 * @param inset Inset from the viewport edge in pixels.
 */
static void border(PresetCanvas* c, const float* v, unsigned base, float inset)
{
    float size  = fminf(.5f, fmaxf(0, v[base])) * MILK_VIEWPORT_HALF_HEIGHT;
    float alpha = fminf(1, fmaxf(0, v[base + 4]));

    if (size <= 0 || alpha <= 0)
    {
        return;
    }

    float        x0 = inset, y0 = inset, x1 = DISPLAY_WIDTH - inset, y1 = DISPLAY_HEIGHT - inset;
    PresetVertex a = vertex(x0, y0, v, base + 1), b = a, d = a, e = a;

    b.x = x1;
    d.x = x1;
    d.y = y0 + size;
    e.y = y0 + size;

    preset_quad(c, a, b, d, e, alpha);

    a.y = y1 - size;
    b.y = y1 - size;
    d.y = y1;
    e.y = y1;

    preset_quad(c, a, b, d, e, alpha);

    a.x = x0;
    a.y = y0 + size;
    b.x = x0 + size;
    b.y = a.y;
    d.x = b.x;
    d.y = y1 - size;
    e.x = x0;
    e.y = d.y;

    preset_quad(c, a, b, d, e, alpha);

    a.x = x1 - size;
    b.x = x1;
    d.x = x1;
    e.x = x1 - size;

    preset_quad(c, a, b, d, e, alpha);
}

void milk_smooth_wave(const MusicFeatures* audio, float scale, float smoothing, float wave[2][MILK_AUDIO_SAMPLES])
{
    smoothing = fminf(1, fmaxf(0, milk_finite(smoothing)));
    scale     = milk_finite(scale);

    float input_mix = scale * (1 - smoothing);

    for (unsigned channel = 0; channel < 2; ++channel)
    {
        wave[channel][0] = milk_finite(audio->milk_audio.waveform[channel][0] * (scale / 128));

        for (unsigned i = 1; i < MILK_AUDIO_SAMPLES; ++i)
        {
            wave[channel][i] = milk_finite(audio->milk_audio.waveform[channel][i] * (input_mix / 128) + wave[channel][i - 1] * smoothing);
        }
    }
}

/**
 * @brief Resolve one preset echo with a transition weight.
 *
 * @param p Preset, or NULL for a disabled echo.
 * @param weight Opacity multiplier.
 * @return Bounded echo settings; disabled for absent presets.
 */
static MilkEcho echo_parameters(const Preset* p, float weight)
{
    MilkEcho echo = { 1, 0, 0, 0 };

    if (!p)
    {
        return echo;
    }

    const float* v = p->milk.frame;

    echo.zoom  = fminf(100, fmaxf(.2f, milk_finite(v[ML_ECHO_ZOOM])));
    echo.alpha = fminf(1, fmaxf(0, milk_finite(v[ML_ECHO_ALPHA]))) * weight;

    float orientation = fmodf(truncf(milk_finite(v[ML_ECHO_ORIENT])), 4);

    if (orientation < 0)
    {
        orientation += 4;
    }

    echo.orientation = (unsigned)orientation;
    echo.wrap        = v[ML_WRAP] != 0;

    return echo;
}

/**
 * @brief Build deterministic animated corner colors without advancing equation RNG.
 *
 * Classic fShader modulates corner colors; it is unrelated to HLSL shaders.
 *
 * @param p Preset, or NULL for neutral colors.
 * @param shade Destination RGB multipliers for TL, TR, BL, BR.
 */
static void corner_shade(const Preset* p, float shade[4][3])
{
    float amount = p ? fminf(1, fmaxf(0, p->milk.frame[ML_SHADER])) : 0;

    if (amount == 0)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            for (unsigned c = 0; c < 3; ++c)
            {
                shade[i][c] = 1;
            }
        }

        return;
    }

    float    phase[3] = { 0 };
    uint32_t seed     = p ? p->seed : 1;

    for (unsigned c = 0; c < 3; ++c)
    {
        seed     = seed * SHADE_SEED_MULTIPLIER + SHADE_SEED_INCREMENT;
        phase[c] = (seed >> 8) * (PRESET_TAU / 16777216.0f);
    }

    for (unsigned i = 0; i < 4; ++i)
    {
        float t      = p ? p->milk.inputs[0] : 0;
        float rgb[3] = { .6f + .3f * sinf(t * .429f + 3 + i * 21 + phase[0]), .6f + .3f * sinf(t * .321f + 1 + i * 13 + phase[1]), .6f + .3f * sinf(t * .387f + 6 + i * 9 + phase[2]) };
        float peak   = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2]));

        for (unsigned c = 0; c < 3; ++c)
        {
            shade[i][c] = 1 - amount + amount * (.5f + .5f * rgb[c] / peak);
        }
    }
}

void milk_shade_at(const MilkComposite* composite, float x, float y, float rgb[3])
{
    const float (*s)[3] = composite->shade;

    for (unsigned c = 0; c < 3; ++c)
    {
        rgb[c] = x + y <= 1 ? s[0][c] + x * (s[1][c] - s[0][c]) + y * (s[2][c] - s[0][c]) : s[3][c] + (1 - y) * (s[1][c] - s[3][c]) + (1 - x) * (s[2][c] - s[3][c]);
    }
}

void milk_composite(const Preset* a, const Preset* b, float mix, MilkComposite* composite)
{
    mix                = b ? fminf(1, fmaxf(0, milk_finite(mix))) : 0;
    composite->echo[0] = echo_parameters(a, 1 - mix);
    composite->echo[1] = echo_parameters(b, mix);
    composite->base    = fmaxf(0, 1 - composite->echo[0].alpha - composite->echo[1].alpha);

    float first[4][3], second[4][3];

    corner_shade(a, first);
    corner_shade(b, second);

    float ga = a ? a->milk.frame[ML_GAMMA] : 1;
    float gb = b ? b->milk.frame[ML_GAMMA] : 1;

    composite->gamma = fminf(8, fmaxf(0, milk_finite(ga * (1 - mix) + gb * mix)));

    const Preset* chosen = b && mix >= .5f ? b : a;

    composite->brighten = chosen && milk_truth(chosen->milk.frame[ML_BRIGHTEN]);
    composite->darken   = chosen && milk_truth(chosen->milk.frame[ML_DARKEN]);
    composite->solarize = chosen && milk_truth(chosen->milk.frame[ML_SOLARIZE]);
    composite->invert   = chosen && milk_truth(chosen->milk.frame[ML_INVERT]);
    composite->shaded   = 0;

    for (unsigned i = 0; i < 4; ++i)
    {
        for (unsigned c = 0; c < 3; ++c)
        {
            composite->shade[i][c] = first[i][c] * (1 - mix) + second[i][c] * mix;

            if (composite->shade[i][c] < .99999f)
            {
                composite->shaded = 1;
            }
        }
    }
}

void milk_echo_uv(const MilkEcho* echo, float x, float y, float* u, float* v)
{
    float zoom = fminf(100, fmaxf(.2f, milk_finite(echo->zoom)));

    *u = .5f + (x - .5f) / zoom;
    *v = .5f + (y - .5f) / zoom;

    if (echo->orientation & 1)
    {
        *u = 1 - *u;
    }

    if (echo->orientation & 2)
    {
        *v = 1 - *v;
    }
}

void milk_wave_shape(const Preset* p, const MusicFeatures* audio, const float wave[2][MILK_AUDIO_SAMPLES], MilkWave* shape)
{
    const float* v    = p->milk.frame;
    unsigned     mode = (unsigned)fminf(8, fmaxf(0, v[ML_WAVE_MODE]));

    memset(shape, 0, sizeof(*shape));

    shape->break_at = shape->break_at2 = MILK_WAVE_MAX;
    shape->closed                      = mode == 0;
    shape->count                       = mode <= 1 ? 240 : mode == 4 ? 213
                                                       : mode == 6   ? 213
                                                       : mode == 7   ? 426
                                                       : mode == 8   ? MILK_SPECTRUM_POINTS
                                                                     : 480;

    float    mystery    = v[ML_WAVE_MYSTERY];
    float    position_x = v[ML_WAVE_X] * 2 - 1, position_y = v[ML_WAVE_Y] * 2 - 1;
    float    start_x = 0, start_y = 0, dx = 0, dy = 0, perp_x = 0, perp_y = 0;
    unsigned length = mode == 7 ? 213 : shape->count;

    if (mode >= 6)
    {
        float angle = 1.57f * mystery;

        dx = cosf(angle);
        dy = sinf(angle);

        float edge_x[2] = { position_x * cosf(angle + 1.57f) - dx * 3, position_x * cosf(angle + 1.57f) + dx * 3 };
        float edge_y[2] = { position_x * sinf(angle + 1.57f) - dy * 3, position_x * sinf(angle + 1.57f) + dy * 3 };

        for (unsigned i = 0; i < 2; ++i)
        {
            for (unsigned edge = 0; edge < 4; ++edge)
            {
                float value = edge < 2 ? edge_x[i] : edge_y[i];
                float other = edge < 2 ? edge_x[1 - i] : edge_y[1 - i];
                float bound = edge & 1 ? -1.1f : 1.1f;

                if ((edge & 1 ? value < bound : value > bound) && fabsf(value - other) > 1e-8f)
                {
                    float t = (bound - other) / (value - other);

                    edge_x[i] = edge_x[1 - i] + (edge_x[i] - edge_x[1 - i]) * t;
                    edge_y[i] = edge_y[1 - i] + (edge_y[i] - edge_y[1 - i]) * t;
                }
            }
        }

        start_x = edge_x[0];
        start_y = edge_y[0];
        dx      = (edge_x[1] - start_x) / length;
        dy      = (edge_y[1] - start_y) / length;

        float perpendicular = atan2f(dy, dx) + 1.57f;

        perp_x = cosf(perpendicular);
        perp_y = sinf(perpendicular);

        if (mode == 7)
        {
            shape->break_at = length;
        }
    }

    if ((mode == 0 || mode == 1 || mode == 4) && (mystery < -1 || mystery > 1))
    {
        mystery = mystery * .5f + .5f;
        mystery = fabsf(mystery - floorf(mystery)) * 2 - 1;
    }

    float momentum     = .45f + .5f * (mystery * .5f + .5f);
    float rotation_cos = cosf(p->milk.inputs[0] * .3f), rotation_sin = sinf(p->milk.inputs[0] * .3f);

    for (unsigned i = 0; i < shape->count; ++i)
    {
        float x, y;

        if (mode <= 3)
        {
            float right = wave[1][i + (mode == 0 ? 120 : 0)], left = wave[0][i + 32];

            if (mode >= 2)
            {
                x = right;
                y = left;
            }
            else
            {
                float radius = (mode == 0 ? .5f : .53f) + (mode == 0 ? .4f : .43f) * right + mystery;

                if (mode == 0 && i < shape->count / 10)
                {
                    float mix   = .5f - .5f * cosf(i / (shape->count * .1f) * 3.1416f);
                    float other = .5f + .4f * wave[1][i + shape->count + 120] + mystery;

                    radius = other * (1 - mix) + radius * mix;
                }

                float angle = mode == 0 ? i * (PRESET_TAU / (shape->count - 1)) + p->milk.inputs[0] * .2f : left * 1.57f + p->milk.inputs[0] * 2.3f;

                x = radius * cosf(angle);
                y = radius * sinf(angle);
            }

            shape->point[i] = (MilkPoint){ v[ML_WAVE_X] * DISPLAY_WIDTH + x * MILK_VIEWPORT_HALF_HEIGHT, (1 - v[ML_WAVE_Y]) * DISPLAY_HEIGHT - y * MILK_VIEWPORT_HALF_HEIGHT };
        }
        else if (mode == 4)
        {
            unsigned offset = (480 - shape->count) / 2;

            x               = -1 + 2 * ((float)i / shape->count) + position_x + wave[1][i + offset + 25] * .44f;
            y               = wave[0][i + offset] * .47f + position_y;
            shape->point[i] = (MilkPoint){ (1 + x) * MILK_VIEWPORT_HALF_WIDTH, (1 - y) * MILK_VIEWPORT_HALF_HEIGHT };

            if (i > 1)
            {
                shape->point[i].x = shape->point[i].x * (1 - momentum) + momentum * (2 * shape->point[i - 1].x - shape->point[i - 2].x);
                shape->point[i].y = shape->point[i].y * (1 - momentum) + momentum * (2 * shape->point[i - 1].y - shape->point[i - 2].y);
            }
        }
        else if (mode == 5)
        {
            float x0 = wave[1][i] * wave[0][i + 32] + wave[0][i] * wave[1][i + 32];
            float y0 = wave[1][i] * wave[1][i] - wave[0][i + 32] * wave[0][i + 32];

            x               = x0 * rotation_cos - y0 * rotation_sin;
            y               = x0 * rotation_sin + y0 * rotation_cos;
            shape->point[i] = (MilkPoint){ v[ML_WAVE_X] * DISPLAY_WIDTH + x * MILK_VIEWPORT_HALF_HEIGHT, (1 - v[ML_WAVE_Y]) * DISPLAY_HEIGHT - y * MILK_VIEWPORT_HALF_HEIGHT };
        }
        else
        {
            unsigned sample = i % length;
            float    displacement;

            if (mode == 8)
            {
                displacement = .1f * logf(fmaxf(1e-6f, audio->spectrum_left[sample]));
            }
            else
            {
                unsigned channel = mode == 7 && i >= length ? 1 : 0;

                displacement = .25f * wave[channel][sample + (480 - length) / 2];

                if (mode == 7)
                {
                    displacement += (channel ? -1 : 1) * v[ML_WAVE_Y] * v[ML_WAVE_Y];
                }
            }

            x               = start_x + dx * sample + perp_x * displacement;
            y               = start_y + dy * sample + perp_y * displacement;
            shape->point[i] = (MilkPoint){ (1 + x) * MILK_VIEWPORT_HALF_WIDTH, (1 - y) * MILK_VIEWPORT_HALF_HEIGHT };
        }
    }
}

/**
 * @brief Draw center darkening and inner/outer borders.
 *
 * @param p MilkDrop preset.
 * @param c Drawing sink.
 */
static void decorations(const Preset* p, PresetCanvas* c)
{
    const float* v = p->milk.frame;

    preset_blend(c, 0);

    if (v[ML_DARKEN_CENTER] != 0 && c->darken_center && c->opacity > 0)
    {
        c->darken_center(c->context, c->opacity);
    }

    border(c, v, ML_OB_SIZE, 0);
    border(c, v, ML_IB_SIZE, fminf(.5f, fmaxf(0, v[ML_OB_SIZE])) * MILK_VIEWPORT_HALF_HEIGHT);
}

void milk_draw(const Preset* p, PresetCanvas* c, const MusicFeatures* audio)
{
    milk_objects_draw(p, c);
    milk_wave_draw(p, NULL, 0, c, audio);
    decorations(p, c);
}

void milk_draw_transition(const Preset* old, const Preset* next, float mix, PresetCanvas* c, const MusicFeatures* audio)
{
    float opacity = c->opacity;

    c->opacity = opacity * (1 - mix);

    milk_objects_draw(old, c);

    c->opacity = opacity * mix;

    milk_objects_draw(next, c);

    c->opacity = opacity;

    milk_wave_draw(old, next, mix, c, audio);

    c->opacity = opacity * (1 - mix);

    decorations(old, c);

    c->opacity = opacity * mix;

    decorations(next, c);

    c->opacity = opacity;
}
