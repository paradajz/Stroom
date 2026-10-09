#include "contracts/milkdrop.h"
#include "platform/graphics/display_config.h"
#include "unity.h"
#include "ui/visualizer/scene.h"
#include "ui/visualizer/scene_batch.h"
#include "ui/visualizer/color_curve.h"
#include <string.h>

/* Exercise scene lifetime through the public renderer API with a recording SDK. */
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
static int           allocation_failure;
static unsigned      clears, curve_closes;
static u64           captured_colors[3];

/**
 * @brief Interpret the queued fixture GS register packet.
 */
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

/**
 * @brief Substitute scene_color_curve_close in this host fixture.
 */
void scene_color_curve_close(void)
{
    ++curve_closes;
}

/**
 * @brief Substitute scene_color_curve in this host fixture.
 */
int scene_color_curve(GSGLOBAL* gs, int brighten, int darken, int solarize)
{
    (void)gs;
    execute_packet();

    content[target] += (brighten ? 20 : 0) + (darken ? 10 : 0) + (solarize ? 40 : 0);

    return 0;
}

/**
 * @brief Substitute gsKit_heap_alloc in this host fixture.
 */
void* gsKit_heap_alloc(GSGLOBAL* gs, int qwords, int bytes, int type)
{
    (void)gs;
    (void)qwords;
    (void)type;
    /* Consume the previous reservation using its finalized queue tail. The
     * renderer must flush and trim an open batch before allocating again. */
    execute_packet();
    memset(packet, 0, sizeof(packet));

    packet_pending = 1;
    packet_bytes   = bytes;
    queue.pool_cur = (uint8_t*)packet + bytes + 16;
    queue.tag_size = qwords + 1;

    return packet;
}

/**
 * @brief Substitute gsKit_float_to_int_x in this host fixture.
 */
int gsKit_float_to_int_x(GSGLOBAL* gs, float x)
{
    return (int)(x * 16) + gs->OffsetX;
}

/**
 * @brief Substitute gsKit_float_to_int_y in this host fixture.
 */
int gsKit_float_to_int_y(GSGLOBAL* gs, float y)
{
    return (int)(y * 16) + gs->OffsetY;
}

/**
 * @brief Substitute gsKit_vram_alloc in this host fixture.
 */
u32 gsKit_vram_alloc(GSGLOBAL* gs, u32 size, u8 type)
{
    (void)type;
    TEST_ASSERT_EQUAL_UINT(gs->Width * gs->Height * 4, size);
    ++allocations;

    return allocation_failure ? GSKIT_ALLOC_ERROR : 3 * PS2_GS_FRAME_PAGE_BYTES;
}

/**
 * @brief Substitute gsKit_clear in this host fixture.
 */
void gsKit_clear(GSGLOBAL* gs, u64 color)
{
    (void)gs;
    (void)color;
    execute_packet();

    ++clears;

    content[target] = 1;
}

/**
 * @brief Substitute gsKit_set_clamp in this host fixture.
 */
void gsKit_set_clamp(GSGLOBAL* gs, int mode)
{
    (void)gs;
    TEST_ASSERT_EQUAL_INT(GS_CMODE_REGION_CLAMP, mode);
}

/**
 * @brief Substitute gsKit_set_primalpha in this host fixture.
 */
void gsKit_set_primalpha(GSGLOBAL* gs, u64 mode, u8 per_pixel)
{
    (void)per_pixel;

    gs->PrimAlpha = mode;
}

/**
 * @brief Substitute gsKit_prim_sprite in this host fixture.
 */
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

/**
 * @brief Substitute gsKit_prim_sprite_texture in this host fixture.
 */
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

/**
 * @brief Substitute gsKit_prim_triangle_gouraud in this host fixture.
 */
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

/**
 * @brief Substitute gsKit_prim_triangle_texture in this host fixture.
 */
void gsKit_prim_triangle_texture(GSGLOBAL* gs, GSTEXTURE* texture, float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2, int z, u64 color)
{
    (void)gs;
    (void)texture;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)z;
    (void)color;
    execute_packet();

    captured_colors[2] = color;
    captured_colors[1] = color;
    captured_colors[0] = color;

    ++texture_setups;

    if (verify_order)
    {
        TEST_ASSERT_EQUAL_FLOAT(triangles, x0);
    }

    const float uv[] = { u0, v0, u1, v1, u2, v2 };

    memcpy(captured_uv, uv, sizeof(uv));
    ++triangles;
}

/**
 * @brief Substitute gsKit_prim_triangle_goraud_texture in this host fixture.
 */
void gsKit_prim_triangle_goraud_texture(GSGLOBAL* gs, GSTEXTURE* texture, float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2, float y2, float u2, float v2, int z, u64 c0, u64 c1, u64 c2)
{
    (void)c1;
    (void)c2;
    gsKit_prim_triangle_texture(gs, texture, x0, y0, u0, v0, x1, y1, u1, v1, x2, y2, u2, v2, z, c0);

    captured_colors[1] = c1;
    captured_colors[2] = c2;
}

/**
 * @brief Prepare fixture state before each test.
 */
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
    curve_closes                          = 0;
    clears                                = 0;
    allocations                           = 0;
    triangle_packets                      = 0;
    texture_setups                        = 0;
    triangles                             = 0;
    verify_order                          = 0;
    allocation_failure                    = 0;
    target                                = 1;

    memset(content, 0, sizeof(content));
}

/**
 * @brief Finish fixture cleanup and resource checks after each test.
 */
void tearDown(void)
{
    scene_close();
}

/**
 * @brief Verify reopen reserves history and clears previous contents.
 */
static void reopen_reserves_history_and_clears_previous_contents(void)
{
    scene_draw(&drawing, &director, &music, 1, 1);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(1, allocations);
    TEST_ASSERT_EQUAL_UINT(1, clears);
    scene_draw(&drawing, &director, &music, 1, 1);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(1, allocations);
    TEST_ASSERT_EQUAL_UINT(1, clears);
    scene_close();
    TEST_ASSERT_EQUAL_UINT(1, curve_closes);
    /* The next graphics context can reuse the same address. */
    scene_draw(&drawing, &director, &music, 1, 1);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(2, allocations);
    TEST_ASSERT_EQUAL_UINT(2, clears);
}

/**
 * @brief Verify failed allocation can retry in next context.
 */
static void failed_allocation_can_retry_in_next_context(void)
{
    allocation_failure = 1;

    scene_draw(&drawing, &director, &music, 1, 1);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(1, allocations);

    allocation_failure = 0;

    scene_draw(&drawing, &director, &music, 1, 1);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(1, allocations);
    scene_close();
    scene_draw(&drawing, &director, &music, 1, 1);
    execute_packet();
    TEST_ASSERT_EQUAL_UINT(2, allocations);
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(reopen_reserves_history_and_clears_previous_contents);
    RUN_TEST(failed_allocation_can_retry_in_next_context);

    return UNITY_END();
}
