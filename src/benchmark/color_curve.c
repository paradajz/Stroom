#include "benchmark/color_curve.h"
#include "platform/graphics/display.h"
#include "platform/graphics/readback.h"
#include "ui/visualizer/color_curve.h"
#include "ui/visualizer/color_curve_layout.h"
#include "milkdrop/milk.h"
#include "platform/graphics/display_config.h"
#include "platform/graphics/constants.h"
#include <stdint.h>
#include <malloc.h>

#define PATTERN_GREEN_X_STEP 7u
#define PATTERN_BLUE_X_STEP  13u
#define PATTERN_BLUE_Y_STEP  17u

static u32 pixel(unsigned x, unsigned y)
{
    return ((x + y * 3) & UINT8_MAX) | (((x * PATTERN_GREEN_X_STEP + y) & UINT8_MAX) << 8) | (((x * PATTERN_BLUE_X_STEP + y * PATTERN_BLUE_Y_STEP) & UINT8_MAX) << 16) | PS2_GS_OPAQUE_ALPHA;
}

/* Reference the half-resolution pipeline, including its deliberate spatial loss.
 * GS bilinear interpolation truncates after horizontal and vertical filtering.
 * Keep those stages separate rather than averaging all four texels at once. */
static unsigned curve_channel(int x, int y, unsigned channel, unsigned mode)
{
    const int width  = DISPLAY_WIDTH / SCENE_COLOR_CURVE_SCALE;
    const int height = DISPLAY_HEIGHT / SCENE_COLOR_CURVE_SCALE;

    x = x < 0 ? 0 : x >= width ? width - 1
                               : x;
    y = y < 0 ? 0 : y >= height ? height - 1
                                : y;

    unsigned sum = 0;

    for (unsigned dy = 0; dy < SCENE_COLOR_CURVE_SCALE; ++dy)
    {
        unsigned row = 0;

        for (unsigned dx = 0; dx < SCENE_COLOR_CURVE_SCALE; ++dx)
        {
            row += (pixel(x * SCENE_COLOR_CURVE_SCALE + dx, y * SCENE_COLOR_CURVE_SCALE + dy) >> (channel * 8)) & UINT8_MAX;
        }

        sum += row / SCENE_COLOR_CURVE_SCALE;
    }

    unsigned value = sum / SCENE_COLOR_CURVE_SCALE;

    if (mode & MILKDROP_COLOR_CURVE_BRIGHTEN)
    {
        unsigned inverse = UINT8_MAX - value;

        value = UINT8_MAX - (inverse * inverse + (UINT8_MAX / 2)) / UINT8_MAX;
    }

    if (mode & MILKDROP_COLOR_CURVE_DARKEN)
    {
        value = (value * value + (UINT8_MAX / 2)) / UINT8_MAX;
    }

    if (mode & MILKDROP_COLOR_CURVE_SOLARIZE)
    {
        value = 2 * ((value * (UINT8_MAX - value) + (UINT8_MAX / 2)) / UINT8_MAX);
    }

    return value;
}

static unsigned reference(unsigned x, unsigned y, unsigned mode)
{
    unsigned value = PS2_GS_OPAQUE_ALPHA;

    for (unsigned channel = 0; channel < 3; ++channel)
    {
        int      left = (int)(x / 2) - !(x & 1);
        int      top  = (int)(y / 2) - !(y & 1);
        unsigned wx = x & 1 ? 1 : 3, wy = y & 1 ? 1 : 3;
        unsigned top_color    = (4 - wx) * curve_channel(left, top, channel, mode) + wx * curve_channel(left + 1, top, channel, mode);
        unsigned bottom_color = (4 - wx) * curve_channel(left, top + 1, channel, mode) + wx * curve_channel(left + 1, top + 1, channel, mode);

        value |= (((4 - wy) * (top_color / 4) + wy * (bottom_color / 4)) / 4) << (channel * 8);
    }

    return value;
}

static int verify_curve(GSGLOBAL* gs, FILE* events, unsigned mode)
{
    u32* image = memalign(128, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(u32));

    if (!image)
    {
        return BENCHMARK_COLOR_CURVE_ERROR_MEMORY;
    }

    for (unsigned y = 0; y < DISPLAY_HEIGHT; ++y)
    {
        for (unsigned x = 0; x < DISPLAY_WIDTH; ++x)
        {
            image[y * DISPLAY_WIDTH + x] = pixel(x, y);
        }
    }

    unsigned target = gs->ScreenBuffer[gs->ActiveBuffer & 1];

    gsKit_texture_send(image, DISPLAY_WIDTH, DISPLAY_HEIGHT, target, GS_PSM_CT32, DISPLAY_WIDTH / 64, GS_CLUT_NONE);

    int success = scene_color_curve(gs, mode & MILKDROP_COLOR_CURVE_BRIGHTEN, mode & MILKDROP_COLOR_CURVE_DARKEN, mode & MILKDROP_COLOR_CURVE_SOLARIZE) == 0;

    platform_display_submit(gs);

    unsigned       mismatches = 0;
    unsigned       max_error  = 0;
    const unsigned tolerance  = 3;
    unsigned       first_x = 0, first_y = 0, first_expected = 0, first_actual = 0;

    /* Full-width strips stay within the readback DMA size limit. */

    for (unsigned y = 0; y < DISPLAY_HEIGHT && success; y += 32)
    {
        success = platform_display_readback(gs, image, target, y, 32) == 0;

        if (!success)
        {
            break;
        }

        for (unsigned row = 0; row < 32; ++row)
        {
            for (unsigned x = 0; x < DISPLAY_WIDTH; ++x)
            {
                unsigned expected = reference(x, y + row, mode);
                unsigned actual   = image[row * DISPLAY_WIDTH + x];
                int      mismatch = (actual & PS2_GS_ALPHA_MASK) != (expected & PS2_GS_ALPHA_MASK);

                for (unsigned channel = 0; channel < 3; ++channel)
                {
                    unsigned a          = (actual >> (channel * 8)) & UINT8_MAX;
                    unsigned e          = (expected >> (channel * 8)) & UINT8_MAX;
                    unsigned difference = a > e ? a - e : e - a;

                    if (difference > max_error)
                    {
                        max_error = difference;
                    }

                    mismatch |= difference > tolerance;
                }

                if (mismatch)
                {
                    if (!mismatches)
                    {
                        first_x        = x;
                        first_y        = y + row;
                        first_expected = expected;
                        first_actual   = actual;
                    }

                    ++mismatches;
                }
            }
        }
    }

    fprintf(events, "{\"event\":\"color_curve_validation\",\"mode\":%u,\"pixels\":%u,\"mismatches\":%u,\"readback\":%s,\"scale\":%u,\"tolerance\":%u,\"max_channel_error\":%u,\"first_mismatch\":", mode, DISPLAY_WIDTH * DISPLAY_HEIGHT, mismatches, success ? "true" : "false", SCENE_COLOR_CURVE_SCALE, tolerance, max_error);

    if (mismatches)
    {
        fprintf(events, "{\"x\":%u,\"y\":%u,\"expected\":%u,\"actual\":%u}", first_x, first_y, first_expected, first_actual);
    }
    else
    {
        fputs("null", events);
    }

    fputs("}\n", events);
    free(image);

    return !success ? BENCHMARK_COLOR_CURVE_ERROR_RENDER : mismatches ? BENCHMARK_COLOR_CURVE_ERROR_MISMATCH
                                                                      : 0;
}

int benchmark_verify_color_curves(GSGLOBAL* gs, FILE* events)
{
    int needed = 0;

    for (unsigned i = 0; i < MILK_PRESET_COUNT; ++i)
    {
        needed |= milk_programs[i].defaults[ML_DARKEN] != 0 || milk_programs[i].defaults[ML_BRIGHTEN] != 0 || milk_programs[i].defaults[ML_SOLARIZE] != 0;
    }

    if (!needed)
    {
        return 0;
    }

    platform_display_submit(gs);

    /* Exercise palette switching as well as each curve, outside measured frames. */

    for (unsigned mode = 1; mode <= MILKDROP_COLOR_CURVE_MASK; ++mode)
    {
        int result = verify_curve(gs, events, mode);

        if (result != 0)
        {
            return result;
        }
    }

    return 0;
}
