/* Classic motion vectors and center darkening adapted from MilkDrop DrawMotionVectors/DrawSprites. */
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

#define MIX(field) (motion->field = first.field + (second.field - first.field) * mix)

/**
 * @brief Replace non-finite input with zero and clamp to a range.
 *
 * @param x Input value.
 * @param lo Lower bound.
 * @param hi Upper bound.
 * @return Finite, clamped value.
 */
static float bound(float x, float lo, float hi)
{
    return fminf(hi, fmaxf(lo, milk_finite(x)));
}

/**
 * @brief Read and bound one preset motion-vector configuration.
 *
 * @param p Preset, or NULL for disabled vectors.
 * @return Resolved motion-vector settings.
 */
static MilkMotion parameters(const Preset* p)
{
    MilkMotion m = { 0 };

    if (!p)
    {
        return m;
    }

    const float* v = p->milk.frame;

    m.x      = bound(v[ML_MV_X], 0, MILK_MOTION_X);
    m.y      = bound(v[ML_MV_Y], 0, MILK_MOTION_Y);
    m.dx     = bound(v[ML_MV_DX], -1, 1);
    m.dy     = bound(v[ML_MV_DY], -1, 1);
    m.length = bound(v[ML_MV_L], -100, 100);
    m.r      = bound(v[ML_MV_R], 0, 1);
    m.g      = bound(v[ML_MV_G], 0, 1);
    m.b      = bound(v[ML_MV_B], 0, 1);
    m.alpha  = bound(v[ML_MV_A], 0, 1);

    return m;
}

int milk_motion_parameters(const Preset* a, const Preset* b, float mix, MilkMotion* motion)
{
    MilkMotion first = parameters(a), second = b ? parameters(b) : first;

    mix = b ? bound(mix, 0, 1) : 0;

    MIX(x);
    MIX(y);
    MIX(dx);
    MIX(dy);
    MIX(length);
    MIX(r);
    MIX(g);
    MIX(b);
    MIX(alpha);

    return motion->alpha >= .001f && motion->x >= 1 && motion->y >= 1;
}

/**
 * @brief Bilinearly sample feedback UVs without executing vertex equations.
 *
 * @param mesh Feedback mesh.
 * @param x Normalized horizontal coordinate strictly inside 0..1.
 * @param y Normalized vertical coordinate strictly inside 0..1.
 * @return Interpolated texture coordinates.
 */
static FeedbackUV sample_mesh(const FeedbackMesh* mesh, float x, float y)
{
    float      gx = x * FEEDBACK_X, gy = y * FEEDBACK_Y;
    unsigned   ix = (unsigned)gx, iy = (unsigned)gy;
    float      dx = gx - ix, dy = gy - iy;
    unsigned   n = iy * (FEEDBACK_X + 1) + ix;
    FeedbackUV a = mesh->uv[n], b = mesh->uv[n + 1], c = mesh->uv[n + FEEDBACK_X + 1], d = mesh->uv[n + FEEDBACK_X + 2];

    return (FeedbackUV){ (a.u * (1 - dx) + b.u * dx) * (1 - dy) + (c.u * (1 - dx) + d.u * dx) * dy,
                         (a.v * (1 - dx) + b.v * dx) * (1 - dy) + (c.v * (1 - dx) + d.v * dx) * dy };
}

void milk_motion_draw(const MilkMotion* motion, const FeedbackMesh* mesh, PresetCanvas* canvas)
{
    if (motion->alpha < .001f || canvas->opacity <= 0)
    {
        return;
    }

    float    grid_x = bound(motion->x, 0, MILK_MOTION_X), grid_y = bound(motion->y, 0, MILK_MOTION_Y);
    unsigned nx = (unsigned)grid_x, ny = (unsigned)grid_y;

    if (!nx || !ny)
    {
        return;
    }

    preset_blend(canvas, 0);

    for (unsigned y = 0; y < ny; ++y)
    {
        for (unsigned x = 0; x < nx; ++x)
        {
            float fx = (x + .25f) / (grid_x - .75f) + motion->dx;
            float fy = 1 - ((y + .25f) / (grid_y - .75f) - motion->dy);

            if (fx <= .0001f || fx >= .9999f || fy <= .0001f || fy >= .9999f)
            {
                continue;
            }

            FeedbackUV uv = sample_mesh(mesh, fx, fy);
            float      dx = (uv.u - fx) * motion->length, dy = (uv.v - fy) * motion->length;
            float      length = sqrtf(dx * dx + dy * dy);

            if (length < 1.0f / DISPLAY_WIDTH)
            {
                if (length > 1e-8f)
                {
                    dx /= length * DISPLAY_WIDTH;
                    dy /= length * DISPLAY_WIDTH;
                }
                else
                {
                    dx = 1.0f / DISPLAY_WIDTH;
                    dy = -1.0f / DISPLAY_WIDTH;
                }
            }

            /* Start is inside; clip the endpoint along the same direction. */
            float t = 1;

            if (fx + dx < 0)
            {
                t = fminf(t, -fx / dx);
            }

            if (fx + dx > 1)
            {
                t = fminf(t, (1 - fx) / dx);
            }

            if (fy + dy < 0)
            {
                t = fminf(t, -fy / dy);
            }

            if (fy + dy > 1)
            {
                t = fminf(t, (1 - fy) / dy);
            }

            PresetVertex a = { fx * DISPLAY_WIDTH, fy * DISPLAY_HEIGHT, motion->r * 255, motion->g * 255, motion->b * 255 }, b = a;

            b.x = bound(fx + dx * t, 0, 1) * DISPLAY_WIDTH;
            b.y = bound(fy + dy * t, 0, 1) * DISPLAY_HEIGHT;

            preset_line(canvas, a, b, motion->alpha);
        }
    }
}
