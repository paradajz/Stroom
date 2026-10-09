#include "ui/visualizer/color_curve.h"
#include "ui/visualizer/color_curve_layout.h"
#include "platform/graphics/constants.h"
#include "platform/graphics/display_config.h"
#include "platform/memory/cache.h"
#include <dmaKit.h>
#include <malloc.h>
#include <stdio.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <gsInline.h>

#pragma GCC diagnostic pop

#define CURVE_WIDTH  (DISPLAY_WIDTH / SCENE_COLOR_CURVE_SCALE)
#define CURVE_HEIGHT (DISPLAY_HEIGHT / SCENE_COLOR_CURVE_SCALE)
#define STRIP_HEIGHT (32 / SCENE_COLOR_CURVE_SCALE)
#define TILE_WIDTH   (DISPLAY_WIDTH / 2)
#define TILES        (2 / SCENE_COLOR_CURVE_SCALE)
#define SPRITES      (TILE_WIDTH / 4 * STRIP_HEIGHT / 2)
#define PACKET_WORDS (2 + SPRITES * 4)

_Static_assert(DISPLAY_WIDTH % 128 == 0 && CURVE_HEIGHT % STRIP_HEIGHT == 0, "Color curves require whole GS pages and strips");
_Static_assert(DISPLAY_WIDTH < 1024, "Color curve indexed texture coordinates must fit UV");

static u64* packets;
static u32  scratch, palette_vram;
static u32  palette[MILKDROP_COLOR_CURVE_MASK][256] __attribute__((aligned(128)));
static int  initialized, failed;
static u32  reduced;

void scene_color_curve_close(void)
{
    free(packets);

    packets = NULL;
    scratch = palette_vram = reduced = 0;
    initialized = failed = 0;
}

static void reg(GSGLOBAL* gs, u64 value, u64 address)
{
    u64* q = gsKit_heap_alloc(gs, 1, 16, GIF_AD);

    *q++ = GIF_TAG_AD(1);
    *q++ = GIF_AD;
    *q++ = value;
    *q++ = address;
}

static int prepare(GSGLOBAL* gs)
{
    if (initialized || failed)
    {
        return initialized;
    }

    packets      = memalign(128, 3 * TILES * PACKET_WORDS * sizeof(u64));
    scratch      = gsKit_vram_alloc(gs, CURVE_WIDTH * 32 * 4, GSKIT_ALLOC_SYSBUFFER);
    palette_vram = gsKit_vram_alloc(gs, sizeof(palette), GSKIT_ALLOC_SYSBUFFER);

    reduced = gsKit_vram_alloc(gs, gsKit_texture_size(CURVE_WIDTH, CURVE_HEIGHT, GS_PSM_CT32), GSKIT_ALLOC_SYSBUFFER);

    if (reduced == GSKIT_ALLOC_ERROR)
    {
        failed = 1;
    }

    if (failed || !packets || scratch == GSKIT_ALLOC_ERROR || palette_vram == GSKIT_ALLOC_ERROR)
    {
        free(packets);

        packets = NULL;
        failed  = 1;

        printf("stroom: color curve allocation failed\n");
        return 0;
    }

    for (unsigned mode = 1; mode <= MILKDROP_COLOR_CURVE_MASK; ++mode)
    {
        for (unsigned i = 0; i < 256; ++i)
        {
            unsigned value = color_curve_value(i, mode & MILKDROP_COLOR_CURVE_BRIGHTEN, mode & MILKDROP_COLOR_CURVE_DARKEN, mode & MILKDROP_COLOR_CURVE_SOLARIZE);

            palette[mode - 1][color_curve_palette_index(i)] = value | (value << 8) | (value << 16) | PS2_GS_OPAQUE_ALPHA;
        }
    }

    for (unsigned channel = 0; channel < 3; ++channel)
    {
        for (unsigned half = 0; half < TILES; ++half)
        {
            u64* q = packets + (channel * TILES + half) * PACKET_WORDS;

            *q++ = GIF_TAG(SPRITES, 0, 0, 0, PS2_GIF_REGLIST, 4);
            *q++ = GS_UV | ((u64)GS_XYZ2 << 4) | ((u64)GS_UV << 8) | ((u64)GS_XYZ2 << (3 * PS2_GIF_REGISTER_BITS));

            for (unsigned y = 0; y < STRIP_HEIGHT; y += 2)
            {
                for (unsigned x = 0; x < TILE_WIDTH; x += 4)
                {
                    /* Sample texel centers, including the final pixel in each tiny sprite. */
                    unsigned u  = color_curve_u(x, y, channel) * 16 + 8;
                    unsigned v  = color_curve_v(y, channel) * 16 + 8;
                    unsigned dx = gs->OffsetX + (x + half * TILE_WIDTH) * 16;
                    unsigned dy = gs->OffsetY + y * 16;

                    *q++ = GS_SETREG_UV(u, v);
                    *q++ = GS_SETREG_XYZ2(dx, dy, 1);
                    *q++ = GS_SETREG_UV(u + 4 * 16, v + 2 * 16);
                    *q++ = GS_SETREG_XYZ2(dx + 4 * 16, dy + 2 * 16, 1);
                }
            }
        }
    }

    platform_cache_writeback();

    for (unsigned mode = 0; mode < MILKDROP_COLOR_CURVE_MASK; ++mode)
    {
        gsKit_texture_send_inline(gs, palette[mode], 16, 16, palette_vram + mode * sizeof(palette[0]), GS_PSM_CT32, 1, GS_CLUT_NONE);
    }

    initialized = 1;

    return 1;
}

/* Pixel-center resampling. Explicit UV endpoints avoid gsKit's clipping of
 * the last coordinate to the source width, which changes the sampling step. */
static void resample(GSGLOBAL* gs, u32 source, unsigned width, unsigned height, u32 target, unsigned out_width, unsigned out_height)
{
    unsigned tw = 0, th = 0;

    while ((1u << tw) < width)
    {
        ++tw;
    }

    while ((1u << th) < height)
    {
        ++th;
    }

    reg(gs, GS_SETREG_FRAME(target / 8192, out_width / 64, GS_PSM_CT32, 0), GS_FRAME_1 + gs->PrimContext);
    reg(gs, GS_SETREG_XYOFFSET(gs->OffsetX, gs->OffsetY), GS_XYOFFSET_1 + gs->PrimContext);
    reg(gs, GS_SETREG_SCISSOR(0, out_width - 1, 0, out_height - 1), GS_SCISSOR_1 + gs->PrimContext);
    reg(gs, GS_SETREG_CLAMP(GS_CMODE_REGION_CLAMP, GS_CMODE_REGION_CLAMP, 0, width - 1, 0, height - 1), GS_CLAMP_1 + gs->PrimContext);
    gsKit_set_texfilter(gs, GS_FILTER_LINEAR);
    reg(gs, 0, GS_TEXFLUSH);
    reg(gs, GS_SETREG_TEX0(source / 256, width / 64, GS_PSM_CT32, tw, th, 0, 1, 0, 0, 0, 0, 0), GS_TEX0_1 + gs->PrimContext);
    reg(gs, GS_SETREG_RGBAQ(128, 128, 128, 128, 0), GS_RGBAQ);
    reg(gs, GS_SETREG_PRIM(GS_PRIM_PRIM_SPRITE, 0, 1, 0, 0, 0, 1, gs->PrimContext, 0), GS_PRIM);
    /* Power-of-two tiles keep the GS sprite interpolation step exact at
     * these 2:1 and 1:2 ratios; one screen-wide sprite accumulates error. */

    for (unsigned y = 0; y < out_height; y += 32)
    {
        unsigned h = out_height - y < 32 ? out_height - y : 32;

        for (unsigned x = 0; x < out_width; x += 32)
        {
            unsigned w  = out_width - x < 32 ? out_width - x : 32;
            unsigned u0 = (x * 16 + 8) * width / out_width;
            unsigned v0 = (y * 16 + 8) * height / out_height;
            unsigned u1 = ((x + w) * 16 + 8) * width / out_width;
            unsigned v1 = ((y + h) * 16 + 8) * height / out_height;

            reg(gs, GS_SETREG_UV(u0, v0), GS_UV);
            reg(gs, GS_SETREG_XYZ2(gs->OffsetX + x * 16, gs->OffsetY + y * 16, 1), GS_XYZ2);
            reg(gs, GS_SETREG_UV(u1, v1), GS_UV);
            reg(gs, GS_SETREG_XYZ2(gs->OffsetX + (x + w) * 16, gs->OffsetY + (y + h) * 16, 1), GS_XYZ2);
        }
    }
}

int scene_color_curve(GSGLOBAL* gs, int brighten, int darken, int solarize)
{
    unsigned mode = (brighten ? MILKDROP_COLOR_CURVE_BRIGHTEN : 0) | (darken ? MILKDROP_COLOR_CURVE_DARKEN : 0) | (solarize ? MILKDROP_COLOR_CURVE_SOLARIZE : 0);

    if (!mode)
    {
        return 0;
    }

    if (!prepare(gs))
    {
        return SCENE_COLOR_CURVE_ERROR_PREPARE;
    }

    u32 selected_palette = palette_vram + (mode - 1) * sizeof(palette[0]);
    u32 target           = gs->ScreenBuffer[gs->ActiveBuffer & 1];

    gs->PrimAlphaEnable = GS_SETTING_OFF;

    resample(gs, target, DISPLAY_WIDTH, DISPLAY_HEIGHT, reduced, CURVE_WIDTH, CURVE_HEIGHT);

    target = reduced;

    gsKit_set_texfilter(gs, GS_FILTER_NEAREST);
    gsKit_set_clamp(gs, GS_CMODE_CLAMP);
    /* The scratch strip prevents texture/framebuffer aliasing. Reuse immutable
     * DMA packets for each strip; XYOFFSET supplies its destination Y position.
     * The reduced strip keeps T8 UV coordinates below the GS 1024 limit. */

    for (unsigned y = 0; y < CURVE_HEIGHT; y += STRIP_HEIGHT)
    {
        /* Exact VRAM copy: no interpolated texture coordinates at strip edges. */
        reg(gs, GS_SETREG_BITBLTBUF(target / 256, CURVE_WIDTH / 64, GS_PSM_CT32, scratch / 256, CURVE_WIDTH / 64, GS_PSM_CT32), GS_BITBLTBUF);
        reg(gs, GS_SETREG_TRXPOS(0, y, 0, 0, 0), GS_TRXPOS);
        reg(gs, GS_SETREG_TRXREG(CURVE_WIDTH, STRIP_HEIGHT), GS_TRXREG);
        reg(gs, GS_SETREG_TRXDIR(2), GS_TRXDIR);
        reg(gs, 0, GS_TEXFLUSH);
        reg(gs, GS_SETREG_XYOFFSET(gs->OffsetX, gs->OffsetY - y * 16), GS_XYOFFSET_1 + gs->PrimContext);
        reg(gs, GS_SETREG_PRIM(GS_PRIM_PRIM_SPRITE, 0, 1, 0, 0, 0, 1, gs->PrimContext, 0), GS_PRIM);

        for (unsigned channel = 0; channel < 3; ++channel)
        {
            reg(gs, GS_SETREG_FRAME(target / 8192, CURVE_WIDTH / 64, GS_PSM_CT32, ~(0xffu << (channel * 8))), GS_FRAME_1 + gs->PrimContext);

            for (unsigned half = 0; half < TILES; ++half)
            {
                u32 base = scratch + half * (TILE_WIDTH / 64) * 8192;

                reg(gs, GS_SETREG_TEX0(base / 256, CURVE_WIDTH / 32, GS_PSM_T8, 10, 6, 1, 1, selected_palette / 256, GS_PSM_CT32, 0, 0, 1), GS_TEX0_1 + gs->PrimContext);

                u64* packet = packets + (channel * TILES + half) * PACKET_WORDS;
                u64* q      = gsKit_heap_alloc_dma(gs, 1, 16);

                *q++ = DMA_TAG(PACKET_WORDS / 2, 0, DMA_REF, 0, (u32)packet, 0);
                *q++ = 0;
            }
        }
    }

    reg(gs, GS_SETREG_XYOFFSET(gs->OffsetX, gs->OffsetY), GS_XYOFFSET_1 + gs->PrimContext);
    reg(gs, GS_SETREG_FRAME(target / 8192, CURVE_WIDTH / 64, GS_PSM_CT32, 0), GS_FRAME_1 + gs->PrimContext);
    resample(gs, reduced, CURVE_WIDTH, CURVE_HEIGHT, gs->ScreenBuffer[gs->ActiveBuffer & 1], DISPLAY_WIDTH, DISPLAY_HEIGHT);

    return 0;
}
