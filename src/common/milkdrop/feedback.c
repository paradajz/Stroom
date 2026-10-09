/* Classic MilkDrop UV transform adapted from ComputeGridAlphaValues in
 * MilkDrop3/code/vis_milk2/milkdropfs.cpp. See the full BSD notice below.
 * PS2 changes: LUT trig, bounded normalized UVs and one shared GS texture mesh. */
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
#include <string.h>

/* Fixed display geometry, shared by both transition sides and radial zoom.
 * Equation inputs are copied into mutable preset state on every evaluation. */
static struct
{
    float           normalized_y; /**< Texture-space vertical coordinate before aspect correction. */
    MilkVertexInput vertex;       /**< Fixed inputs for the preset's vertex equations. */
} mesh_points[FEEDBACK_VERTICES];

static int mesh_points_ready;

/**
 * @brief Per-transition-side cache of radial zoom powers.
 */
static struct
{
    int   valid;                    /**< Nonzero when cached powers are initialized. */
    float zoom_exp;                 /**< Zoom exponent used to build the cache. */
    float power[FEEDBACK_VERTICES]; /**< Precomputed zoom power for each mesh vertex. */
} radial_cache[2];

/**
 * @brief Clamp a value to an inclusive range.
 *
 * @param x Input value.
 * @param lo Lower bound.
 * @param hi Upper bound.
 * @return Clamped value.
 */
static float clamp(float x, float lo, float hi)
{
    return fminf(hi, fmaxf(lo, x));
}

static void feedback_prepare(FeedbackTransform* f)
{
    float t = f->warp_time;

    f->oscillators[0] = 11.68f + 4 * preset_cos(t * 1.413f + 10);
    f->oscillators[1] = 8.77f + 3 * preset_cos(t * 1.113f + 7);
    f->oscillators[2] = 10.54f + 3 * preset_cos(t * 1.233f + 3);
    f->oscillators[3] = 11.49f + 4 * preset_cos(t * .933f + 5);
}

/**
 * @brief Apply feedback warping using precomputed radial terms.
 *
 * @param f Prepared transform.
 * @param x Normalized horizontal position.
 * @param y Normalized vertical position.
 * @param exponent Precomputed radial zoom exponent.
 * @return Bounded feedback texture coordinates.
 */
static FeedbackUV uv_radial(const FeedbackTransform* f, float x, float y, float exponent)
{
    /* Aspect correction before deformation, undone before GS texture lookup. */
    float px = 2 * x - 1, py = (1 - 2 * y) * MILK_VIEWPORT_ASPECT;
    float zoom = clamp(f->zoom, .001f, 100);
    float z    = exponent == 1 ? zoom : powf(zoom, exponent);
    float u = px * .5f / z + .5f, v = -py * .5f / z + .5f;

    u = (u - f->cx) / f->sx + f->cx;
    v = (v - f->cy) / f->sy + f->cy;

    float        k = f->warp * .0035f, scale = 1 / f->warp_scale, t = f->warp_time;
    const float* o = f->oscillators;

    u += k * preset_sin(t * .333f + scale * (px * o[0] - py * o[3]));
    v += k * preset_cos(t * .375f - scale * (px * o[2] + py * o[1]));
    u += k * preset_cos(t * .753f - scale * (px * o[1] - py * o[2]));
    v += k * preset_sin(t * .825f + scale * (px * o[0] + py * o[3]));

    float rotation = f->rotation;
    float c = preset_cos(rotation), s = preset_sin(rotation), du = u - f->cx, dv = v - f->cy;

    u = du * c - dv * s + f->cx - f->dx;
    v = du * s + dv * c + f->cy - f->dy;
    v = (v - .5f) / MILK_VIEWPORT_ASPECT + .5f;

    /* Clamp framebuffer edges to avoid sampling power-of-two texture padding. */

    if (!isfinite(u))
    {
        u = x;
    }

    if (!isfinite(v))
    {
        v = y;
    }

    return (FeedbackUV){ clamp(x + (u - x), f->wrap ? -2 : 0, f->wrap ? 3 : 1), clamp(y + (v - y), f->wrap ? -2 : 0, f->wrap ? 3 : 1) };
}

/**
 * @brief Cache fixed grid inputs and zoom powers for one transition side.
 *
 * @param side Cache slot: 0 outgoing, 1 incoming.
 * @param zoom_exp Frame-level zoom exponent.
 */
static void prepare_radial(unsigned side, float zoom_exp)
{
    if (!mesh_points_ready)
    {
        for (unsigned y = 0; y <= FEEDBACK_Y; ++y)
        {
            for (unsigned x = 0; x <= FEEDBACK_X; ++x)
            {
                unsigned index = y * (FEEDBACK_X + 1) + x;
                float    nx = (float)x / FEEDBACK_X, ny = (float)y / FEEDBACK_Y;
                float    px = 2 * nx - 1, py = (1 - 2 * ny) * MILK_VIEWPORT_ASPECT;

                mesh_points[index].normalized_y = ny;
                mesh_points[index].vertex       = (MilkVertexInput){ nx, .5f + (ny - .5f) * MILK_VIEWPORT_ASPECT, sqrtf(px * px + py * py), atan2f(py, px) };
            }
        }

        mesh_points_ready = 1;
    }

    if (radial_cache[side].valid && radial_cache[side].zoom_exp == zoom_exp)
    {
        return;
    }

    for (unsigned i = 0; i < FEEDBACK_VERTICES; ++i)
    {
        radial_cache[side].power[i] = zoom_exp == 1 ? 1 : powf(zoom_exp, mesh_points[i].vertex.radius * 2 - 1);
    }

    radial_cache[side].zoom_exp = zoom_exp;
    radial_cache[side].valid    = 1;
}

/**
 * @brief Map a mesh vertex using cached radial powers when possible.
 *
 * @param f Prepared per-vertex transform.
 * @param side Transition cache slot, 0 or 1.
 * @param index Mesh vertex index.
 * @param x Normalized horizontal position.
 * @param y Normalized vertical position.
 * @return Bounded feedback texture coordinates.
 */
static FeedbackUV mesh_uv(const FeedbackTransform* f, unsigned side, unsigned index, float x, float y)
{
    float rad      = mesh_points[index].vertex.radius;
    float exponent = f->zoom_exp == radial_cache[side].zoom_exp ? radial_cache[side].power[index] : f->zoom_exp == 1 ? 1
                                                                                                                     : powf(f->zoom_exp, rad * 2 - 1);

    return uv_radial(f, x, y, exponent);
}

/**
 * @brief Resolve and prepare a preset frame-level feedback transform.
 *
 * @param w Preset state.
 * @return Transform with warp oscillators prepared.
 */
static FeedbackTransform transform(const Preset* w)
{
    FeedbackTransform f = { 0 };

    milk_frame_transform(w, &f);
    feedback_prepare(&f);

    return f;
}

void feedback_build(Preset* a, Preset* b, float mix, FeedbackMesh* mesh)
{
    FeedbackTransform first = transform(a), second = first;

    if (b)
    {
        second = transform(b);
    }

    prepare_radial(0, first.zoom_exp);

    if (b)
    {
        prepare_radial(1, second.zoom_exp);
    }

    milk_begin_vertices(a);

    if (b)
    {
        milk_begin_vertices(b);
    }

    mesh->wrap = (mix < .5f ? first.wrap : second.wrap);

    float first_decay  = first.decay;
    float second_decay = second.decay;

    mesh->decay = first_decay + (second_decay - first_decay) * mix;

    for (unsigned y = 0; y <= FEEDBACK_Y; ++y)
    {
        for (unsigned x = 0; x <= FEEDBACK_X; ++x)
        {
            unsigned               index = y * (FEEDBACK_X + 1) + x;
            const MilkVertexInput* input = &mesh_points[index].vertex;
            float                  px = input->x, py = mesh_points[index].normalized_y;
            FeedbackTransform      local = first;

            milk_transform(a, input, &local);

            if (local.warp_time != first.warp_time)
            {
                feedback_prepare(&local);
            }
            else
            {
                memcpy(local.oscillators, first.oscillators, sizeof(local.oscillators));
            }

            FeedbackUV uv = mesh_uv(&local, 0, index, px, py);

            if (b)
            {
                local = second;

                milk_transform(b, input, &local);

                if (local.warp_time != second.warp_time)
                {
                    feedback_prepare(&local);
                }
                else
                {
                    memcpy(local.oscillators, second.oscillators, sizeof(local.oscillators));
                }

                FeedbackUV next  = mesh_uv(&local, 1, index, px, py);
                float      blend = feedback_transition(mix, px, py, b->seed);

                uv.u += (next.u - uv.u) * blend;
                uv.v += (next.v - uv.v) * blend;
            }

            mesh->uv[index] = uv;
        }
    }
}
