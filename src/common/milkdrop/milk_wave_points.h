#pragma once

#include "milkdrop/milk.h"
#include "milkdrop/viewport.h"
#include "profiling/benchmark.h"
#include <string.h>

/* Expand each wave's equations inside the complete point loop. Keep arithmetic,
 * state writes and profiling boundaries identical to the scalar runtime path.
 * The variadic body has its own scope for compiler-generated temporaries. */
#define MILK_WAVE_POINT_LOOP(name, ...)                                                            \
    static void name(MilkObjectState* s, const float data[2][MILK_OBJECT_SAMPLES], unsigned count) \
    {                                                                                              \
        float*       v   = s->point;                                                               \
        const float* f   = s->frame;                                                               \
        uint32_t*    rng = &s->random;                                                             \
        (void)rng;                                                                                 \
        memcpy(v + MO_Q1, f + MO_Q1, MILK_OBJECT_POINT_REGISTERS * sizeof(float));                 \
                                                                                                   \
        for (unsigned j = 0; j < count; ++j)                                                       \
        {                                                                                          \
            PROFILE_BEGIN(inputs_begin);                                                           \
            v[MO_SAMPLE] = count > 1 ? (float)j / (count - 1) : 0;                                 \
            v[MO_VALUE1] = data[0][j];                                                             \
            v[MO_VALUE2] = data[1][j];                                                             \
            v[MO_X]      = .5f + data[0][j];                                                       \
            v[MO_Y]      = .5f + data[1][j];                                                       \
            memcpy(v + MO_R, f + MO_R, 4 * sizeof(float));                                         \
            PROFILE_BEGIN(equations_begin);                                                        \
            { __VA_ARGS__ } PROFILE_BEGIN(store_begin);                                            \
            s->cache.wave[j] = milk_wave_vertex(v);                                                \
            PROFILE_BEGIN(point_end);                                                              \
            PROFILE_SPAN(wave_point_inputs, inputs_begin, equations_begin);                        \
            PROFILE_SPAN(wave_point_equations, equations_begin, store_begin);                      \
            PROFILE_SPAN(wave_vertex_store, store_begin, point_end);                               \
        }                                                                                          \
    }

/** @brief Store one point with the original clamping and coordinate arithmetic. */
static inline MilkVertex milk_wave_vertex(const float* v)
{
    return (MilkVertex){ milk_bound(v[MO_X], -4, 5) * DISPLAY_WIDTH, MILK_VIEWPORT_HALF_HEIGHT + (milk_bound(v[MO_Y], -4, 5) - .5f) * DISPLAY_WIDTH, milk_bound(v[MO_R], 0, 1) * 255, milk_bound(v[MO_G], 0, 1) * 255, milk_bound(v[MO_B], 0, 1) * 255, milk_bound(v[MO_A], 0, 1), 0, 0 };
}
