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
#include "milkdrop/viewport.h"
#include <math.h>

static float    field[FEEDBACK_Y + 1][FEEDBACK_X + 1];
static uint32_t cached_seed;
static int      ready;
static unsigned sequence;

/**
 * @brief Sample the seeded plasma sequence and advance its index.
 *
 * @param seed Transition seed.
 * @return Deterministic fraction in [0, 1).
 */
static float noise(uint32_t seed)
{
    return preset_hash(seed, sequence++);
}

/**
 * @brief Recursively fill a transition-field rectangle by midpoint displacement.
 *
 * The field is built once per transition seed.
 *
 * @param x0 Left grid index.
 * @param x1 Right grid index.
 * @param y0 Top grid index.
 * @param y1 Bottom grid index.
 * @param amount Displacement amplitude.
 * @param seed Transition seed.
 */
static void plasma(unsigned x0, unsigned x1, unsigned y0, unsigned y1, float amount, uint32_t seed)
{
    unsigned x = (x0 + x1) / 2, y = (y0 + y1) / 2;
    float    a = field[y0][x0], b = field[y0][x1], c = field[y1][x0], d = field[y1][x1];

    if (y1 - y0 >= 2)
    {
        if (x0 == 0)
        {
            field[y][x0] = .5f * (a + c) + (noise(seed) * 2 - 1) * amount * .8f;
        }

        field[y][x1] = .5f * (b + d) + (noise(seed) * 2 - 1) * amount * .8f;
    }

    if (x1 - x0 >= 2)
    {
        if (y0 == 0)
        {
            field[y0][x] = .5f * (a + b) + (noise(seed) * 2 - 1) * amount;
        }

        field[y1][x] = .5f * (c + d) + (noise(seed) * 2 - 1) * amount;
    }

    if (x1 - x0 >= 2 && y1 - y0 >= 2)
    {
        field[y][x] = .25f * (field[y][x0] + field[y][x1] + field[y0][x] + field[y1][x]) + (noise(seed) * 2 - 1) * amount;

        plasma(x0, x, y0, y, amount * .5f, seed);
        plasma(x, x1, y0, y, amount * .5f, seed);
        plasma(x0, x, y, y1, amount * .5f, seed);
        plasma(x, x1, y, y1, amount * .5f, seed);
    }
}

/**
 * @brief Sample the cached seeded transition field, rebuilding it when needed.
 *
 * @param x Normalized horizontal position, clamped to 0..1.
 * @param y Normalized vertical position, clamped to 0..1.
 * @param seed Transition seed.
 * @return Interpolated field value in 0..1.
 */
static float plasma_position(float x, float y, uint32_t seed)
{
    if (!ready || cached_seed != seed)
    {
        sequence                      = 420;
        field[0][0]                   = noise(seed);
        field[0][FEEDBACK_X]          = noise(seed);
        field[FEEDBACK_Y][0]          = noise(seed);
        field[FEEDBACK_Y][FEEDBACK_X] = noise(seed);

        plasma(0, FEEDBACK_X, 0, FEEDBACK_Y, .25f, seed);

        float lo = field[0][0], hi = lo;

        for (unsigned y = 0; y <= FEEDBACK_Y; ++y)
        {
            for (unsigned x = 0; x <= FEEDBACK_X; ++x)
            {
                lo = fminf(lo, field[y][x]);
                hi = fmaxf(hi, field[y][x]);
            }
        }

        for (unsigned y = 0; y <= FEEDBACK_Y; ++y)
        {
            for (unsigned x = 0; x <= FEEDBACK_X; ++x)
            {
                field[y][x] = (field[y][x] - lo) / fmaxf(1e-6f, hi - lo);
            }
        }

        cached_seed = seed;
        ready       = 1;
    }

    float    gx = fminf(1, fmaxf(0, x)) * FEEDBACK_X, gy = fminf(1, fmaxf(0, y)) * FEEDBACK_Y;
    unsigned ix = (unsigned)gx, iy = (unsigned)gy;

    if (ix >= FEEDBACK_X)
    {
        ix = FEEDBACK_X - 1;
    }

    if (iy >= FEEDBACK_Y)
    {
        iy = FEEDBACK_Y - 1;
    }

    float dx = gx - ix, dy = gy - iy;

    return (field[iy][ix] * (1 - dx) + field[iy][ix + 1] * dx) * (1 - dy) + (field[iy + 1][ix] * (1 - dx) + field[iy + 1][ix + 1] * dx) * dy;
}

float feedback_transition(float progress, float x, float y, uint32_t seed)
{
    if (progress <= 0)
    {
        return 0;
    }

    if (progress >= 1)
    {
        return 1;
    }

    unsigned type = seed % 3;
    float    position, band;

    if (type == 0)
    {
        float angle = preset_hash(seed, 410) * 6.28f;

        position = ((x - .5f) * cosf(angle) + (y * MILK_VIEWPORT_ASPECT - .5f) * sinf(angle)) / sqrtf(2) + .5f;
        band     = .1f + .2f * preset_hash(seed, 411);
    }
    else if (type == 1)
    {
        position = plasma_position(x, y, seed);
        band     = .12f + .13f * preset_hash(seed, 411);
    }
    else
    {
        float dx = x - .5f, dy = (y - .5f) * MILK_VIEWPORT_ASPECT;

        position = sqrtf(dx * dx + dy * dy) * 1.41421f;

        if (preset_hash(seed, 412) < .5f)
        {
            position = 1 - position;
        }

        band = .02f + .14f * preset_hash(seed, 411) + .34f * preset_hash(seed, 413);
    }

    return fminf(1, fmaxf(0, (progress * (1 + band) - 1 + position) / band));
}
