#include "contracts/milkdrop.h"
#include "unity.h"
#include "ui/visualizer/scene.h"
#include "ui/visualizer/scene_batch.h"
#include "ui/visualizer/color_curve.h"
#include "platform/graphics/display_config.h"
#include "milkdrop/feedback.h"
#include <string.h>

/* Observe the public renderer through recording graphics and engine adapters. */
static GSGLOBAL      drawing;
static GSQUEUE       queue;
static Director      director;
static MusicFeatures music;
static uint64_t      packet[2048];
static int           packet_pending, packet_bytes;
static unsigned      target, allocations, content[4];
static float         captured_uv[6], captured_xy[6];
static unsigned      triangles, texture_setups, triangle_packets;
static int           verify_order;
static u64           captured_colors[3];
static void (*draw_hook)(PresetCanvas*);
static unsigned      mesh_triangles, echo_triangles;
static int           composite_override;
static MilkComposite composite_fixture;

static void execute_packet(void)
{
    if (packet_pending)
    {
        packet_bytes = (uint8_t*)queue.pool_cur - (uint8_t*)packet - 16;
    }

    if (packet_pending && packet_bytes == 16 && packet[3] == GS_FRAME_1)
    {
        target = (unsigned)(packet[2] & 0xffff);

        TEST_ASSERT_LESS_THAN_UINT(4, target);
    }

    if (packet_pending && packet_bytes > 16 && packet[3] == PS2_GIF_PRIM)
    {
        unsigned registers = packet[4] >> PS2_GIF_NREG_SHIFT;
        unsigned count     = packet[4] & 0x7fff;

        if (registers == 7 || registers == 9)
        {
            ++triangle_packets;
            TEST_ASSERT_EQUAL_UINT((scene_batch_words(count, registers) - 2) * 8, packet_bytes);
            TEST_ASSERT_EQUAL_UINT(registers == 9, (packet[2] >> 3) & 1);
            TEST_ASSERT_EQUAL_UINT(1, (packet[2] >> 4) & 1);
            TEST_ASSERT_EQUAL_UINT(1, (packet[2] >> 8) & 1);

            unsigned cursor = SCENE_BATCH_HEADER_WORDS;

            for (unsigned n = 0; n < count; ++n)
            {
                for (unsigned i = 0; i < 3; ++i)
                {
                    if (registers == 9 || i == 0)
                    {
                        captured_colors[i] = packet[cursor++];
                    }

                    if (registers == 7)
                    {
                        captured_colors[i] = captured_colors[0];
                    }

                    u64 uv = packet[cursor++], xy = packet[cursor++];

                    captured_uv[2 * i]     = (uv & 0x3fff) / 16.0f;
                    captured_uv[2 * i + 1] = ((uv >> 16) & 0x3fff) / 16.0f;
                    captured_xy[2 * i]     = ((xy & 0xffff) - drawing.OffsetX) / 16.0f;
                    captured_xy[2 * i + 1] = (((xy >> 16) & 0xffff) - drawing.OffsetY) / 16.0f;
                }

                if (verify_order)
                {
                    TEST_ASSERT_EQUAL_FLOAT(triangles, captured_xy[0]);
                }

                ++triangles;
            }
        }
    }

    packet_pending = 0;
}

void scene_color_curve_close(void)
{}

int scene_color_curve(GSGLOBAL* gs, int brighten, int darken, int solarize)
{
    (void)gs;
    execute_packet();

    content[target] += (brighten ? 20 : 0) + (darken ? 10 : 0) + (solarize ? 40 : 0);

    return 0;
}

void* gsKit_heap_alloc(GSGLOBAL* gs, int qwords, int bytes, int type)
{
    (void)gs;
    (void)qwords;
    (void)type;
    execute_packet();
    memset(packet, 0, sizeof(packet));

    packet_pending = 1;
    packet_bytes   = bytes;
    queue.pool_cur = (uint8_t*)packet + bytes + 16;
    queue.tag_size = qwords + 1;

    return packet;
}

int gsKit_float_to_int_x(GSGLOBAL* gs, float x)
{
    return (int)(x * 16) + gs->OffsetX;
}

int gsKit_float_to_int_y(GSGLOBAL* gs, float y)
{
    return (int)(y * 16) + gs->OffsetY;
}

u32 gsKit_vram_alloc(GSGLOBAL* gs, u32 size, u8 type)
{
    (void)type;
    TEST_ASSERT_EQUAL_UINT(gs->Width * gs->Height * 4, size);
    ++allocations;

    return 3 * PS2_GS_FRAME_PAGE_BYTES;
}

void gsKit_clear(GSGLOBAL* gs, u64 color)
{
    (void)gs;
    (void)color;
    execute_packet();

    content[target] = 1;
}

void gsKit_set_clamp(GSGLOBAL* gs, int mode)
{
    (void)gs;
    TEST_ASSERT_EQUAL_INT(GS_CMODE_REGION_CLAMP, mode);
}

void gsKit_set_primalpha(GSGLOBAL* gs, u64 mode, u8 per_pixel)
{
    (void)per_pixel;

    gs->PrimAlpha = mode;
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
    execute_packet();
}

void gsKit_prim_sprite_texture(GSGLOBAL* gs, const GSTEXTURE* texture, float x, float y, float u, float v, float right, float bottom, float uu, float vv, int z, u64 color)
{
    (void)gs;
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
    execute_packet();

    unsigned source = texture->Vram / PS2_GS_FRAME_PAGE_BYTES;

    TEST_ASSERT_LESS_THAN_UINT(4, source);

    content[target] = content[source];
}

void gsKit_prim_triangle_gouraud(GSGLOBAL* gs, float x0, float y0, float x1, float y1, float x2, float y2, int z, u64 c0, u64 c1, u64 c2)
{
    (void)gs;
    (void)z;
    (void)c0;
    (void)c1;
    (void)c2;
    execute_packet();

    const float xy[] = { x0, y0, x1, y1, x2, y2 };

    memcpy(captured_xy, xy, sizeof(xy));
    ++triangles;
}

void gsKit_prim_triangle_texture(GSGLOBAL* gs, GSTEXTURE* texture, float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2, int z, u64 color)
{
    (void)gs;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)z;
    (void)color;
    execute_packet();

    /* Constant-color history represents feedback sampling across an active frame. */
    unsigned source = texture->Vram / PS2_GS_FRAME_PAGE_BYTES;

    TEST_ASSERT_LESS_THAN_UINT(4, source);

    content[target] = content[source];

    captured_colors[0] = captured_colors[1] = captured_colors[2] = color;

    ++texture_setups;

    if (verify_order)
    {
        TEST_ASSERT_EQUAL_FLOAT(triangles, x0);
    }

    const float uv[] = { u0, v0, u1, v1, u2, v2 };

    memcpy(captured_uv, uv, sizeof(uv));
    ++triangles;
}

void gsKit_prim_triangle_goraud_texture(GSGLOBAL* gs, GSTEXTURE* texture, float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2, int z, u64 c0, u64 c1, u64 c2)
{
    (void)c1;
    (void)c2;
    gsKit_prim_triangle_texture(gs, texture, x0, y0, u0, v0, x1, y1, u1, v1, x2, y2, u2, v2, z, c0);

    captured_colors[1] = c1;
    captured_colors[2] = c2;
}

void __real_preset_draw(const Preset* w, PresetCanvas* c, const MusicFeatures* audio);

void __wrap_preset_draw(const Preset* w, PresetCanvas* c, const MusicFeatures* audio)
{
    if (draw_hook)
    {
        draw_hook(c);
    }
    else
    {
        __real_preset_draw(w, c, audio);
    }
}

void __real_milk_composite(const Preset* a, const Preset* b, float mix, MilkComposite* composite);

void __wrap_milk_composite(const Preset* a, const Preset* b, float mix, MilkComposite* composite)
{
    if (composite_override)
    {
        *composite = composite_fixture;
    }
    else
    {
        __real_milk_composite(a, b, mix, composite);
    }
}

static void emit_triangles(unsigned count, FeedbackTriangle draw, void* context)
{
    for (unsigned i = 0; i < count; ++i)
    {
        FeedbackVertex v[3] = { { i, 0, 0, 0 }, { i, 16, 1, 0 }, { i + 1, 8, .5f, 1 } };

        draw(context, v);
    }
}

void __wrap_feedback_emit(const FeedbackMesh* mesh, FeedbackTriangle draw, void* context)
{
    (void)mesh;
    emit_triangles(mesh_triangles, draw, context);
}

void __wrap_feedback_echo_emit(const MilkEcho* echo, FeedbackTriangle draw, void* context)
{
    (void)echo;
    emit_triangles(echo_triangles, draw, context);
}

void setUp(void)
{
    static GsClamp clamp;

    scene_close();
    memset(&drawing, 0, sizeof(drawing));

    drawing.CurQueue        = &queue;
    drawing.Clamp           = &clamp;
    drawing.Width           = DISPLAY_WIDTH;
    drawing.Height          = DISPLAY_HEIGHT;
    drawing.ScreenBuffer[0] = PS2_GS_FRAME_PAGE_BYTES;
    drawing.ScreenBuffer[1] = 2 * PS2_GS_FRAME_PAGE_BYTES;

    memset(&director, 0, sizeof(director));

    director.current.milk.frame[ML_GAMMA] = 1;
    packet_pending                        = 0;
    allocations = triangles = texture_setups = triangle_packets = 0;
    verify_order                                                = 0;
    target                                                      = 1;

    memset(content, 0, sizeof(content));

    draw_hook      = NULL;
    mesh_triangles = echo_triangles = 0;
    composite_override              = 0;
}

void tearDown(void)
{
    scene_close();
}

static void seed_history(void)
{
    drawing.ActiveBuffer = 1;
    target               = 2;

    scene_draw(&drawing, &director, &music, 0, 0);
    execute_packet();

    drawing.ActiveBuffer = 0;
    target               = 1;
    triangles = texture_setups = triangle_packets = 0;
}

static void overlays_preserve_clean_history(void)
{
    seed_history();

    mesh_triangles = 1;

    scene_draw(&drawing, &director, &music, 1, 1);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(1, allocations);
    TEST_ASSERT_EQUAL_UINT(1, content[3]);
    TEST_ASSERT_EQUAL_INT(GS_SETTING_OFF, drawing.PrimAlphaEnable);

    content[1]           = 2;
    drawing.ActiveBuffer = 1;
    target               = 2;

    scene_draw(&drawing, &director, &music, 1, 1);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(1, content[2]);
    TEST_ASSERT_EQUAL_UINT(1, content[3]);
    TEST_ASSERT_EQUAL_UINT(1, allocations);

    content[2]           = 2;
    drawing.ActiveBuffer = 0;
    target               = 1;

    scene_draw(&drawing, &director, &music, 1, 0);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(1, content[1]);
    TEST_ASSERT_EQUAL_UINT(1, allocations);
}

static void feedback_without_overlays_avoids_allocation(void)
{
    seed_history();

    mesh_triangles = 1;

    scene_draw(&drawing, &director, &music, 1, 0);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(0, allocations);
    TEST_ASSERT_EQUAL_UINT(1, content[1]);
}

static void textured_object(PresetCanvas* canvas)
{
    MilkVertex objects[3] = { { .u = 0, .v = 0 }, { .u = 1, .v = 1 }, { .u = .5f, .v = .5f } };

    canvas->object_triangle(canvas->context, objects, 1);
}

static void texture_coordinates_follow_texture_size(void)
{
    drawing.Width  = 320;
    drawing.Height = 240;

    seed_history();

    draw_hook = textured_object;

    scene_draw(&drawing, &director, &music, 1, 0);
    execute_packet();

    const float expected[] = { .5f, .5f, 319.5f, 239.5f, 160, 120 };

    TEST_ASSERT_EQUAL_FLOAT_ARRAY(expected, captured_uv, 6);
    TEST_ASSERT_EQUAL_UINT(1, triangles);
}

static void draw_center(PresetCanvas* canvas)
{
    canvas->darken_center(canvas->context, 1);
}

static void darkening_follows_screen_center(void)
{
    drawing.Width  = 320;
    drawing.Height = 240;
    draw_hook      = draw_center;

    scene_draw(&drawing, &director, &music, 0, 0);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(4, triangles);
    TEST_ASSERT_EQUAL_FLOAT(160, captured_xy[0]);
    TEST_ASSERT_EQUAL_FLOAT(120, captured_xy[1]);
    TEST_ASSERT_EQUAL_FLOAT(160, captured_xy[2]);
    TEST_ASSERT_EQUAL_FLOAT(120 - MILK_CENTER_RADIUS, captured_xy[3]);
    TEST_ASSERT_EQUAL_FLOAT(160 - MILK_CENTER_RADIUS, captured_xy[4]);
    TEST_ASSERT_EQUAL_FLOAT(120, captured_xy[5]);
}

static void textured_batch_boundaries(void)
{
    const unsigned counts[] = { 1, 2, 128, 129, 130, 257, 640 };

    seed_history();

    for (unsigned shaded = 0; shaded < 2; ++shaded)
    {
        for (unsigned c = 0; c < sizeof(counts) / sizeof(*counts); ++c)
        {
            triangles = texture_setups = triangle_packets = 0;
            verify_order                                  = 1;
            mesh_triangles                                = shaded ? 0 : counts[c];
            echo_triangles                                = shaded ? counts[c] : 0;
            composite_override                            = shaded;
            composite_fixture                             = (MilkComposite){ .shaded = 1, .gamma = 1 };

            scene_draw(&drawing, &director, &music, 1, 0);
            execute_packet();
            TEST_ASSERT_EQUAL_UINT(counts[c], triangles);
            TEST_ASSERT_EQUAL_UINT(1, texture_setups);
            TEST_ASSERT_EQUAL_UINT((counts[c] - 1 + 127) / 128, triangle_packets);
        }
    }
}

static void curves_preserve_raw_history(void)
{
    seed_history();

    for (unsigned mode = 1; mode <= MILKDROP_COLOR_CURVE_MASK; ++mode)
    {
        director.current.milk.frame[ML_DARKEN]   = (mode & MILKDROP_COLOR_CURVE_DARKEN) != 0;
        director.current.milk.frame[ML_BRIGHTEN] = mode & MILKDROP_COLOR_CURVE_BRIGHTEN;
        director.current.milk.frame[ML_SOLARIZE] = (mode & MILKDROP_COLOR_CURVE_SOLARIZE) != 0;

        scene_draw(&drawing, &director, &music, 0, 0);
        execute_packet();
        TEST_ASSERT_EQUAL_UINT(1, content[3]);
        TEST_ASSERT_EQUAL_UINT(1 + (mode & MILKDROP_COLOR_CURVE_BRIGHTEN ? 20 : 0) + (mode & MILKDROP_COLOR_CURVE_DARKEN ? 10 : 0) + (mode & MILKDROP_COLOR_CURVE_SOLARIZE ? 40 : 0), content[1]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(curves_preserve_raw_history);
    RUN_TEST(textured_batch_boundaries);
    RUN_TEST(overlays_preserve_clean_history);
    RUN_TEST(feedback_without_overlays_avoids_allocation);
    RUN_TEST(texture_coordinates_follow_texture_size);
    RUN_TEST(darkening_follows_screen_center);

    return UNITY_END();
}
