#pragma once

#include "contracts/milkdrop.h"
#include <stdint.h>

#define COLOR_CURVE_BLOCK_SIDE 8u

/**
 * @brief Map an RGB byte from an 8x8 CT32 block to its T8 horizontal coordinate.
 * The CT32/T8 block mapping preserves contiguous 4x2 rectangles.
 * @param x CT32 pixel column.
 * @param y CT32 pixel row.
 * @param channel RGB channel index from zero to two.
 * @return T8 texture column.
 */
static inline unsigned color_curve_u(unsigned x, unsigned y, unsigned channel)
{
    return (x / COLOR_CURVE_BLOCK_SIDE) * 16 + ((x & (COLOR_CURVE_BLOCK_SIDE - 1)) ^ (((channel & 1) ^ ((y >> 1) & 1)) << 2)) + ((channel & 2) << 2);
}

/**
 * @brief Map an RGB byte from an 8x8 CT32 block to its T8 vertical coordinate.
 * @param y CT32 pixel row.
 * @param channel RGB channel index from zero to two.
 * @return T8 texture row.
 */
static inline unsigned color_curve_v(unsigned y, unsigned channel)
{
    return (y / COLOR_CURVE_BLOCK_SIDE) * 16 + (y & 1) + ((channel & 1) << 1) + ((y & (COLOR_CURVE_BLOCK_SIDE - 2)) << 1);
}

/**
 * @brief Swap CSM1 palette index bits three and four for a CT32 CLUT.
 * @param index Eight-bit logical palette index.
 * @return Physical palette index.
 */
static inline unsigned color_curve_palette_index(unsigned index)
{
    return (index & ~(8u | 16u)) | ((index & 8) << 1) | ((index & 16) >> 1);
}

/**
 * @brief Apply brighten, darken and solarize in sequence with eight-bit rounding.
 * @param value Eight-bit channel value.
 * @param brighten Nonzero to apply invert-square-invert brighten.
 * @param darken Nonzero to square the channel.
 * @param solarize Nonzero to multiply by its inverse and double the result.
 * @return Transformed eight-bit channel value.
 */
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
