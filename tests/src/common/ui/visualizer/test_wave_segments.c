#include "ui/visualizer/scene.h"
#include "ui/visualizer/color_curve.h"
#include "milkdrop/milk_objects.h"
#include "platform/graphics/display_config.h"
#include "unity.h"
#include <string.h>

static GSQUEUE  queues[2];
static uint64_t packets[2][20000];
static unsigned side, trial, direct_calls, thick_calls;
static void (*optimized_segments)(void*, const MilkVertex*, const MilkVertex*, const MilkVertex*, float, unsigned);

void* gsKit_heap_alloc(GSGLOBAL* gs, int qwords, int bytes, int type)
{
    (void)bytes;
    (void)type;

    unsigned  words = (qwords + 1) * 2;
    uint64_t* p     = gs->CurQueue->pool_cur;

    TEST_ASSERT_TRUE(p + words <= packets[side] + 20000);

    gs->CurQueue->pool_cur = p + words;
    gs->CurQueue->tag_size += qwords + 1;

    return p;
}

static int coordinate(float value, int offset)
{
    int n = (int)(value * 16.f) + offset;

    return n < 0 ? 0 : n >= 65536 ? 65535
                                  : n;
}

int gsKit_float_to_int_x(GSGLOBAL* gs, float x)
{
    return coordinate(x, gs->OffsetX);
}

int gsKit_float_to_int_y(GSGLOBAL* gs, float y)
{
    return coordinate(y, gs->OffsetY);
}

u32 gsKit_vram_alloc(GSGLOBAL* gs, u32 size, u8 type)
{
    (void)gs;
    (void)size;
    (void)type;
    TEST_FAIL_MESSAGE("Unexpected history allocation");

    return GSKIT_ALLOC_ERROR;
}

void gsKit_clear(GSGLOBAL* gs, u64 color)
{
    (void)gs;
    (void)color;
}

void gsKit_set_clamp(GSGLOBAL* gs, int mode)
{
    (void)gs;
    (void)mode;
}

void gsKit_set_primalpha(GSGLOBAL* gs, u64 mode, u8 per_pixel)
{
    (void)per_pixel;

    gs->PrimAlpha = mode;
}

void scene_color_curve_close(void)
{}

int scene_color_curve(GSGLOBAL* gs, int brighten, int darken, int solarize)
{
    (void)gs;
    (void)brighten;
    (void)darken;
    (void)solarize;
    TEST_FAIL_MESSAGE("Unexpected presentation curve");

    return -1;
}

void gsKit_prim_sprite(GSGLOBAL* gs, float x, float y, float right, float bottom, int z, u64 color)
{
    (void)gs;
    (void)x;
    (void)y;
    (void)right;
    (void)bottom;
    (void)z;
    (void)color;
    TEST_FAIL_MESSAGE("Unexpected non-wave primitive");
}

void gsKit_prim_sprite_texture(GSGLOBAL* gs, const GSTEXTURE* texture, float x, float y, float u, float v, float right, float bottom, float uu, float vv, int z, u64 color)
{
    (void)gs;
    (void)texture;
    (void)x;
    (void)y;
    (void)u;
    (void)v;
    (void)right;
    (void)bottom;
    (void)uu;
    (void)vv;
    (void)z;
    (void)color;
    TEST_FAIL_MESSAGE("Unexpected non-wave primitive");
}

void gsKit_prim_triangle_gouraud(GSGLOBAL* gs, float x0, float y0, float x1, float y1, float x2, float y2, int z, u64 c0, u64 c1, u64 c2)
{
    (void)gs;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)z;
    (void)c0;
    (void)c1;
    (void)c2;
    TEST_FAIL_MESSAGE("Unexpected non-wave primitive");
}

void gsKit_prim_triangle_texture(GSGLOBAL* gs, GSTEXTURE* texture, float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2, int z, u64 color)
{
    (void)gs;
    (void)texture;
    (void)x0;
    (void)y0;
    (void)u0;
    (void)v0;
    (void)x1;
    (void)y1;
    (void)u1;
    (void)v1;
    (void)x2;
    (void)y2;
    (void)u2;
    (void)v2;
    (void)z;
    (void)color;
    TEST_FAIL_MESSAGE("Unexpected non-wave primitive");
}

void gsKit_prim_triangle_goraud_texture(GSGLOBAL* gs, GSTEXTURE* texture, float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2, int z, u64 c0, u64 c1, u64 c2)
{
    (void)gs;
    (void)texture;
    (void)x0;
    (void)y0;
    (void)u0;
    (void)v0;
    (void)x1;
    (void)y1;
    (void)u1;
    (void)v1;
    (void)x2;
    (void)y2;
    (void)u2;
    (void)v2;
    (void)z;
    (void)c0;
    (void)c1;
    (void)c2;
    TEST_FAIL_MESSAGE("Unexpected non-wave primitive");
}

static float random_unit(uint32_t* state)
{
    *state = *state * 1664525u + 1013904223u;

    return (*state >> 8) / 16777216.0f;
}

static void direct(void* context, const MilkVertex* a, const MilkVertex* mid, const MilkVertex* b, float opacity, unsigned copies)
{
    ++direct_calls;

    if (copies == 4)
    {
        ++thick_calls;
    }

    optimized_segments(context, a, mid, b, opacity, copies);
}

void __wrap_preset_draw(const Preset* w, PresetCanvas* c, const MusicFeatures* audio)
{
    (void)w;
    (void)audio;

    GSGLOBAL* gs            = c->context;
    GSGLOBAL  alternate     = *gs;
    alternate.PrimFogEnable = 1;
    alternate.PrimAAEnable  = 1;
    alternate.PrimContext   = 1;
    alternate.OffsetX       = 31000;
    alternate.OffsetY       = 30000;
    uint32_t rng            = trial + 123;

    for (unsigned i = 0; i < trial % 129; ++i)
    {
        c->sprite(c->context, (PresetVertex){ .x = 2, .y = 3, .r = 5, .g = 6, .b = 7 }, 1, 1);
    }

    optimized_segments = c->object_wave_segments;

    TEST_ASSERT_NOT_NULL(optimized_segments);

    for (unsigned group = 0; group < 40; ++group)
    {
        MilkVertex v[3];

        for (unsigned j = 0; j < 3; ++j)
        {
            float f[8];

            for (unsigned k = 0; k < 8; ++k)
            {
                f[k] = random_unit(&rng);
            }

            v[j] = (MilkVertex){ f[0] * 680 - 20, f[1] * 552 - 20, f[2] * 255, f[3] * 255, f[4] * 255, f[5], f[6], f[7] };
        }

        if (group % 7 == 0)
        {
            v[0].x = trial % 2 ? 640 : -0.0f;
        }

        if (group % 9 == 0)
        {
            v[2].y = trial % 2 ? 511 : 512;
        }

        if (group % 11 == 0)
        {
            c->blend(c->context, group % 2);

            gs->PrimAlphaEnable ^= 1;
            gs->OffsetX += 1;
        }

        PresetCanvas canvas         = *c;
        canvas.context              = group % 13 == 0 ? &alternate : gs;
        canvas.opacity              = (trial % 5) * .25f;
        canvas.object_wave_segments = side ? direct : NULL;

        milk_object_wave_segment(&canvas, v[0], v[1], v[2], group % 5 ? 1 : 4);

        if (group % 17 == 0)
        {
            c->sprite(c->context, (PresetVertex){ .x = 2, .y = 3, .r = 5, .g = 6, .b = 7 }, 1, 1);
        }
    }
}

void setUp(void)
{
    direct_calls = thick_calls = 0;

    scene_close();
}

void tearDown(void)
{
    scene_close();
}

static void wave_packets(void)
{
    for (trial = 0; trial < 1000; ++trial)
    {
        unsigned used[2];

        for (side = 0; side < 2; ++side)
        {
            scene_close();
            memset(packets[side], 0, sizeof(packets[side]));

            queues[side]                          = (GSQUEUE){ .pool_cur = packets[side] };
            GSGLOBAL      gs                      = { .PrimAlphaEnable = 1, .OffsetX = 32768, .OffsetY = 32768, .CurQueue = &queues[side], .Width = DISPLAY_WIDTH, .Height = DISPLAY_HEIGHT };
            Director      director                = { 0 };
            MusicFeatures music                   = { 0 };
            director.current.milk.frame[ML_GAMMA] = 1;

            scene_draw(&gs, &director, &music, 0, 0);

            used[side] = (uint64_t*)queues[side].pool_cur - packets[side];

            TEST_ASSERT_EQUAL_UINT(used[side] / 2, queues[side].tag_size);
        }

        TEST_ASSERT_EQUAL_UINT(used[0], used[1]);
        TEST_ASSERT_EQUAL_MEMORY(packets[0], packets[1], used[0] * sizeof(uint64_t));
    }

    TEST_ASSERT_GREATER_THAN_UINT(10000, direct_calls);
    TEST_ASSERT_GREATER_THAN_UINT(1000, thick_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(wave_packets);

    return UNITY_END();
}
