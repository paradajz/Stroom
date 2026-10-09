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

#include "milkdrop/feedback.h"
#include "unity.h"
#include "milkdrop/viewport.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Clamp a reference texture coordinate.
 *
 * @param x Input coordinate.
 * @param wrap Whether repeated texture coordinates are allowed.
 * @return Value within the texture sampling bounds.
 */
static float limit(float x, int wrap)
{
    return fminf(wrap ? 3 : 1, fmaxf(wrap ? -2 : 0, x));
}

/**
 * @brief Evaluate independent scalar feedback equations with either trig policy.
 *
 * @param f Feedback settings.
 * @param x Normalized horizontal coordinate.
 * @param y Normalized vertical coordinate.
 * @param lookup Nonzero to use the renderer's lookup trig, zero for libm.
 * @return Clamped reference texture coordinates.
 */
static FeedbackUV reference(FeedbackTransform f, float x, float y, int lookup)
{
    float (*sine)(float)   = lookup ? preset_sin : sinf;
    float (*cosine)(float) = lookup ? preset_cos : cosf;

    float px = 2 * x - 1, py = (1 - 2 * y) * MILK_VIEWPORT_ASPECT, rad = sqrtf(px * px + py * py);
    float z = powf(fminf(100, fmaxf(.001f, f.zoom)), powf(f.zoom_exp, rad * 2 - 1));
    float u = (px * .5f / z + .5f - f.cx) / f.sx + f.cx;
    float v = (-py * .5f / z + .5f - f.cy) / f.sy + f.cy;
    float t = f.warp_time, k = f.warp * .0035f, scale = 1 / f.warp_scale;
    float o[] = { 11.68f + 4 * cosine(t * 1.413f + 10), 8.77f + 3 * cosine(t * 1.113f + 7), 10.54f + 3 * cosine(t * 1.233f + 3), 11.49f + 4 * cosine(t * .933f + 5) };

    u += k * (sine(t * .333f + scale * (px * o[0] - py * o[3])) + cosine(t * .753f - scale * (px * o[1] - py * o[2])));
    v += k * (cosine(t * .375f - scale * (px * o[2] + py * o[1])) + sine(t * .825f + scale * (px * o[0] + py * o[3])));

    float du = u - f.cx, dv = v - f.cy;

    u = du * cosine(f.rotation) - dv * sine(f.rotation) + f.cx - f.dx;
    v = du * sine(f.rotation) + dv * cosine(f.rotation) + f.cy - f.dy;
    v = (v - .5f) / MILK_VIEWPORT_ASPECT + .5f;

    return (FeedbackUV){ limit(isfinite(u) ? u : x, f.wrap), limit(isfinite(v) ? v : y, f.wrap) };
}

/**
 * @brief Compare every preset against scalar equations without mesh caches.
 * @param lookup Nonzero for mapping equivalence, zero for libm accuracy.
 */
static void check_mapping(int lookup)
{
    /* Mapping retains its strict numerical tolerance. Independently bound lookup
     * approximation to half one GS UV step: 1/32 texel, mapped as in scene.c.
     * The GS submits texture coordinates in units of 1/16 texel. */
    const float   tolerance_u = lookup ? .00002f : 1.0f / (32 * (DISPLAY_WIDTH - 1));
    const float   tolerance_v = lookup ? .00002f : 1.0f / (32 * (DISPLAY_HEIGHT - 1));
    MusicFeatures audio;

    music_init(&audio);

    audio.time  = 2.7f;
    audio.frame = 135;

    for (unsigned kind = 0; kind < MILK_PRESET_COUNT; ++kind)
    {
        Preset preset;

        preset_init(&preset, kind, 7);
        preset_step(&preset, &audio, .02f);

        /* Repeated and changed exponents exercise cache reuse and invalidation. */
        const float exponents[] = { 1, 1.2f, 1.2f, .9f };

        for (unsigned pass = 0; pass < sizeof(exponents) / sizeof(*exponents); ++pass)
        {
            preset.milk.frame[ML_ZOOMEXP] = exponents[pass];

            Preset       actual_preset = preset, expected_preset = preset;
            FeedbackMesh mesh;

            feedback_build(&actual_preset, NULL, 0, &mesh);
            milk_begin_vertices(&expected_preset);

            for (unsigned y = 0; y <= FEEDBACK_Y; ++y)
            {
                for (unsigned x = 0; x <= FEEDBACK_X; ++x)
                {
                    float             px = (float)x / FEEDBACK_X, py = (float)y / FEEDBACK_Y;
                    FeedbackTransform transform = { 0 };

                    /* Original per-frame calculation, independent of the grid cache. */
                    float           cx = 2 * px - 1, cy = (1 - 2 * py) * MILK_VIEWPORT_ASPECT;
                    MilkVertexInput input = { px, .5f + (py - .5f) * MILK_VIEWPORT_ASPECT, sqrtf(cx * cx + cy * cy), atan2f(cy, cx) };

                    milk_transform(&expected_preset, &input, &transform);

                    FeedbackUV expected = reference(transform, px, py, lookup);
                    FeedbackUV actual   = mesh.uv[y * (FEEDBACK_X + 1) + x];

                    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(tolerance_u, expected.u, actual.u, milk_programs[kind].name);
                    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(tolerance_v, expected.v, actual.v, milk_programs[kind].name);
                }
            }

            /* Cached inputs must preserve persistent variables and RNG order too. */
            TEST_ASSERT_EQUAL_MEMORY(&expected_preset.milk, &actual_preset.milk, sizeof(actual_preset.milk));
        }
    }
}

/** @brief Preserve strict mapping and cache equivalence using the same trig policy. */
static void mapping(void)
{
    check_mapping(1);
}

/** @brief Bound lookup-table error against libm independently of cache equivalence. */
static void lookup_accuracy(void)
{
    check_mapping(0);
}

/**
 * @brief Verify MilkDrop mesh bounds, deterministic evaluation, and transition endpoints.
 */
static void fields(void)
{
    MusicFeatures audio;

    music_init(&audio);

    audio.time  = 3;
    audio.frame = 150;

    FeedbackMesh first, last, mixed, repeat;

    for (unsigned seed = 0; seed < 32; ++seed)
    {
        Preset a, b;

        preset_init(&a, seed % MILK_PRESET_COUNT, seed);
        preset_init(&b, (seed + 1) % MILK_PRESET_COUNT, seed + 70);
        preset_step(&a, &audio, .02f);
        preset_step(&b, &audio, .02f);

        Preset ac = a;
        Preset bc = b;

        feedback_build(&ac, NULL, 0, &first);
        feedback_build(&bc, NULL, 0, &last);

        Preset expected_a = ac, expected_b = bc;

        ac = a;
        bc = b;

        feedback_build(&ac, &bc, 0, &mixed);
        TEST_ASSERT_TRUE_MESSAGE(!memcmp(first.uv, mixed.uv, sizeof(first.uv)), "!memcmp(first.uv, mixed.uv, sizeof(first.uv))");

        ac = a;
        bc = b;

        feedback_build(&ac, &bc, 1, &mixed);

        for (unsigned i = 0; i < FEEDBACK_VERTICES; ++i)
        {
            TEST_ASSERT_TRUE_MESSAGE(fabsf(last.uv[i].u - mixed.uv[i].u) < .000001f, "fabsf(last.uv[i].u - mixed.uv[i].u) < .000001f");
            TEST_ASSERT_TRUE_MESSAGE(fabsf(last.uv[i].v - mixed.uv[i].v) < .000001f, "fabsf(last.uv[i].v - mixed.uv[i].v) < .000001f");
        }

        ac = a;
        bc = b;

        feedback_build(&ac, &bc, .5f, &mixed);
        TEST_ASSERT_EQUAL_MEMORY(&expected_a.milk, &ac.milk, sizeof(ac.milk));
        TEST_ASSERT_EQUAL_MEMORY(&expected_b.milk, &bc.milk, sizeof(bc.milk));

        ac = a;
        bc = b;

        feedback_build(&ac, &bc, .5f, &repeat);
        TEST_ASSERT_TRUE_MESSAGE(!memcmp(&mixed, &repeat, sizeof(mixed)), "!memcmp(&mixed, &repeat, sizeof(mixed))");
        TEST_ASSERT_TRUE_MESSAGE(mixed.decay >= 0 && mixed.decay <= 1, "mixed.decay >= 0 && mixed.decay <= 1");
        TEST_ASSERT_TRUE_MESSAGE(fabsf(mixed.decay - (first.decay + last.decay) * .5f) < .000001f, "fabsf(mixed.decay - (first.decay + last.decay) * .5f) < .000001f");

        for (unsigned i = 0; i < FEEDBACK_VERTICES; ++i)
        {
            TEST_ASSERT_TRUE_MESSAGE(isfinite(mixed.uv[i].u) && mixed.uv[i].u >= -2 && mixed.uv[i].u <= 3, "isfinite(mixed.uv[i].u) && mixed.uv[i].u >= -2 && mixed.uv[i].u <= 3");
            TEST_ASSERT_TRUE_MESSAGE(isfinite(mixed.uv[i].v) && mixed.uv[i].v >= -2 && mixed.uv[i].v <= 3, "isfinite(mixed.uv[i].v) && mixed.uv[i].v >= -2 && mixed.uv[i].v <= 3");
        }

        for (unsigned y = 0; y <= 8; ++y)
        {
            for (unsigned x = 0; x <= 8; ++x)
            {
                float previous = 0;

                for (unsigned tick = 0; tick <= 100; ++tick)
                {
                    float blend = feedback_transition(tick * .01f, x / 8.0f, y / 8.0f, seed);

                    TEST_ASSERT_TRUE_MESSAGE(blend >= previous && blend <= 1, "blend >= previous && blend <= 1");

                    previous = blend;
                }

                TEST_ASSERT_TRUE_MESSAGE(previous == 1, "previous == 1");
            }
        }
    }
}

/**
 * @brief Run regression checks for feedback mappings and transitions.
 *
 */
/**
 * @brief Fixtures are initialized by each scenario.
 */
void setUp(void)
{
    Preset init;

    preset_init(&init, 0, 1);
}

/**
 * @brief This suite owns no external resources.
 */
void tearDown(void)
{}

/**
 * @brief Run the regression scenario.
 * @return Number of failed Unity cases.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(mapping);
    RUN_TEST(lookup_accuracy);
    RUN_TEST(fields);

    return UNITY_END();
}
