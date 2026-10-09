#include "unity.h"
#include "ui/visualizer/color_curve_layout.h"

/* Independently encode the GS byte-address wiring inside one CT32/T8 block. */
static unsigned ct32_byte(unsigned x, unsigned y, unsigned c)
{
    return c | ((x & 1) << 2) | ((y & 1) << 3) | ((x & 6) << 3) | ((y & 6) << 5);
}

static unsigned t8_byte(unsigned x, unsigned y)
{
    return ((y >> 1) & 1) | ((x >> 2) & 2) | ((x & 1) << 2) | ((y & 1) << 3) | ((x & 2) << 3) | ((((x >> 2) ^ (y >> 1) ^ (y >> 2)) & 1) << 5) | ((y & 12) << 4);
}

static void byte_mapping(void)
{
    /* Every pixel of every 4x2 sprite must sample the correct RGB byte, even
     * across page boundaries. Both formats use the same block order. */

    for (unsigned c = 0; c < 3; ++c)
    {
        for (unsigned y = 0; y < 32; y += 2)
        {
            for (unsigned x = 0; x < 320; x += 4)
            {
                unsigned u = color_curve_u(x, y, c), v = color_curve_v(y, c);

                TEST_ASSERT_EQUAL_UINT(x / 8, u / 16);
                TEST_ASSERT_EQUAL_UINT(y / 8, v / 16);

                for (unsigned dy = 0; dy < 2; ++dy)
                {
                    for (unsigned dx = 0; dx < 4; ++dx)
                    {
                        TEST_ASSERT_EQUAL_UINT(ct32_byte((x + dx) & 7, (y + dy) & 7, c), t8_byte((u + dx) & 15, (v + dy) & 15));
                    }
                }
            }
        }
    }
}

static void palette_curve(void)
{
    unsigned seen[256] = { 0 };

    for (unsigned i = 0; i < 256; ++i)
    {
        unsigned value = color_curve_value(i, 0, 1, 0);

        TEST_ASSERT_TRUE(value <= i);
        TEST_ASSERT_EQUAL_UINT(0, seen[color_curve_palette_index(i)]++);
        /* Rounded normalized square, checked against floating point. */
        TEST_ASSERT_EQUAL_UINT((unsigned)(255.0 * (i / 255.0) * (i / 255.0) + .5), value);
    }

    for (unsigned mode = 0; mode <= MILKDROP_COLOR_CURVE_MASK; ++mode)
    {
        for (unsigned i = 0; i < 256; ++i)
        {
            double value = i;

            if (mode & MILKDROP_COLOR_CURVE_BRIGHTEN)
            {
                value = (unsigned)(255 * (1 - (1 - value / 255) * (1 - value / 255)) + .5);
            }

            if (mode & MILKDROP_COLOR_CURVE_DARKEN)
            {
                value = (unsigned)(255 * (value / 255) * (value / 255) + .5);
            }

            if (mode & MILKDROP_COLOR_CURVE_SOLARIZE)
            {
                value = 2 * (unsigned)(255 * (value / 255) * (1 - value / 255) + .5);
            }

            TEST_ASSERT_EQUAL_UINT((unsigned)value, color_curve_value(i, mode & MILKDROP_COLOR_CURVE_BRIGHTEN, mode & MILKDROP_COLOR_CURVE_DARKEN, mode & MILKDROP_COLOR_CURVE_SOLARIZE));
        }
    }

    TEST_ASSERT_EQUAL_UINT(192, color_curve_value(128, 1, 0, 0));
    TEST_ASSERT_EQUAL_UINT(145, color_curve_value(128, 1, 1, 0));
    TEST_ASSERT_EQUAL_UINT(0, color_curve_value(0, 0, 1, 0));
    TEST_ASSERT_EQUAL_UINT(64, color_curve_value(128, 0, 1, 0));
    TEST_ASSERT_EQUAL_UINT(255, color_curve_value(255, 0, 1, 0));
}

void setUp(void)
{}

void tearDown(void)
{}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(byte_mapping);
    RUN_TEST(palette_curve);

    return UNITY_END();
}
