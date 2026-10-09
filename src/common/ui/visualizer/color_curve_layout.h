#pragma once

#include "contracts/milkdrop.h"
#include <stdint.h>

#define COLOR_CURVE_BLOCK_SIDE 8u

/* CT32 and T8 share page/block ordering, but arrange bytes differently inside
 * a block. Map an RGB byte in an 8x8 CT32 block to its T8 texel. Contiguous
 * 4x2 rectangles survive this mapping and can be drawn with one sprite. */
static inline unsigned color_curve_u(unsigned x, unsigned y, unsigned channel)
{
    return (x / COLOR_CURVE_BLOCK_SIDE) * 16 + ((x & (COLOR_CURVE_BLOCK_SIDE - 1)) ^ (((channel & 1) ^ ((y >> 1) & 1)) << 2)) + ((channel & 2) << 2);
}

static inline unsigned color_curve_v(unsigned y, unsigned channel)
{
    return (y / COLOR_CURVE_BLOCK_SIDE) * 16 + (y & 1) + ((channel & 1) << 1) + ((y & (COLOR_CURVE_BLOCK_SIDE - 2)) << 1);
}

/* CSM1 interchanges palette index bits 3 and 4 for a 256-entry CT32 CLUT. */
static inline unsigned color_curve_palette_index(unsigned index)
{
    return (index & ~(8u | 16u)) | ((index & 8) << 1) | ((index & 16) >> 1);
}

/* Classic brighten is invert-square-invert, followed by darken and then solarize.
 * Round each operation independently, matching sequential 8-bit render passes. */
static inline unsigned color_curve_value(unsigned value, int brighten, int darken, int solarize)
{
    if (brighten)
    {
        unsigned inverted = UINT8_MAX - value;

        value = UINT8_MAX - (inverted * inverted + (UINT8_MAX / 2)) / UINT8_MAX;
    }

    if (darken)
    {
        value = (value * value + (UINT8_MAX / 2)) / UINT8_MAX;
    }

    // Classic solarize multiplies by the inverse, then doubles that 8-bit result.
    return solarize ? 2 * ((value * (UINT8_MAX - value) + (UINT8_MAX / 2)) / UINT8_MAX) : value;
}
