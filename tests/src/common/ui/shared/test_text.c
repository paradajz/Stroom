#include "ui/shared/text.h"
#include "ui/shared/style.h"
#include "unity.h"
#include <string.h>

typedef struct
{
    float left, top, right, bottom;
    int   z;
    u64   color;
} Pixel;

static Pixel    pixels[1024];
static unsigned count;

void setUp(void)
{
    count = 0;

    memset(pixels, 0, sizeof(pixels));
}

void tearDown(void)
{}

void gsKit_prim_sprite(GSGLOBAL* gs, float x, float y, float right, float bottom, int z, u64 color)
{
    (void)gs;
    TEST_ASSERT_LESS_THAN_UINT(1024, count);

    pixels[count++] = (Pixel){ x, y, right, bottom, z, color };
}

static void question_pixels(void)
{
    const float coordinates[][2] = { { 10, 20 }, { 12, 20 }, { 14, 22 }, { 12, 24 }, { 12, 28 } };

    ui_text_scaled(NULL, 10, 20, "?", 42, 2);
    TEST_ASSERT_EQUAL_UINT(5, count);

    for (unsigned i = 0; i < count; ++i)
    {
        TEST_ASSERT_EQUAL_FLOAT(coordinates[i][0], pixels[i].left);
        TEST_ASSERT_EQUAL_FLOAT(coordinates[i][1], pixels[i].top);
        TEST_ASSERT_EQUAL_FLOAT(coordinates[i][0] + 2, pixels[i].right);
        TEST_ASSERT_EQUAL_FLOAT(coordinates[i][1] + 2, pixels[i].bottom);
        TEST_ASSERT_EQUAL_INT(1, pixels[i].z);
        TEST_ASSERT_EQUAL_UINT64(42, pixels[i].color);
    }
}

static void supported_characters(void)
{
    const char* supported = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.,-_+&()[]'\"/>:!?#";

    for (const char* ch = supported; *ch; ++ch)
    {
        char text[] = { *ch, 0 };

        count = 0;

        ui_text_scaled(NULL, 0, 0, text, 42, 1);
        TEST_ASSERT_GREATER_THAN_UINT(0, count);

        for (unsigned i = 0; i < count; ++i)
        {
            TEST_ASSERT_TRUE(pixels[i].left >= 0 && pixels[i].right <= 3);
            TEST_ASSERT_TRUE(pixels[i].top >= 0 && pixels[i].bottom <= 5);
        }
    }
}

static void unsupported_characters(void)
{
    Pixel expected[15];

    ui_text_scaled(NULL, 10, 20, "?", 42, 2);

    unsigned expected_count = count;

    TEST_ASSERT_GREATER_THAN_UINT(0, expected_count);
    memcpy(expected, pixels, count * sizeof(*pixels));

    const char* supported = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.,-_+&()[]'\"/>:!?#";

    for (unsigned ch = 1; ch <= 255; ++ch)
    {
        if (strchr(supported, (int)ch))
        {
            continue;
        }

        char text[] = { (char)ch, 0 };

        count = 0;

        ui_text_scaled(NULL, 10, 20, text, 42, 2);
        TEST_ASSERT_EQUAL_UINT(expected_count, count);
        TEST_ASSERT_EQUAL_MEMORY(expected, pixels, count * sizeof(*pixels));
    }
}

static void number_and_text_share_pixels(void)
{
    Pixel expected[512];

    ui_text_scaled(NULL, 11, 13, "0123456789:-", ui_color(UI_COLOR_TEXT), 7.5f);

    unsigned expected_count = count;

    memcpy(expected, pixels, count * sizeof(*pixels));

    count = 0;

    ui_number(NULL, 11, 13, "0123456789:-", 7.5f);
    TEST_ASSERT_EQUAL_UINT(expected_count, count);
    TEST_ASSERT_EQUAL_MEMORY(expected, pixels, count * sizeof(*pixels));
}

static void spaces_and_case(void)
{
    ui_text_scaled(NULL, 0, 0, "   ", 42, 2);
    TEST_ASSERT_EQUAL_UINT(0, count);
    ui_text_scaled(NULL, 0, 0, "a A", 42, 2);
    TEST_ASSERT_EQUAL_UINT(20, count);

    for (unsigned i = 0; i < count / 2; ++i)
    {
        TEST_ASSERT_EQUAL_FLOAT(pixels[i].left + 16, pixels[i + count / 2].left);
        TEST_ASSERT_EQUAL_FLOAT(pixels[i].top, pixels[i + count / 2].top);
        TEST_ASSERT_EQUAL_FLOAT(2, pixels[i].right - pixels[i].left);
        TEST_ASSERT_EQUAL_FLOAT(2, pixels[i].bottom - pixels[i].top);
    }

    TEST_ASSERT_EQUAL_FLOAT(22, ui_text_width("a A", 2));
}

static void horizontal_clip(void)
{
    ui_text_clipped(NULL, -1, 0, "AAAA", 42, 2, 0, 8);
    TEST_ASSERT_GREATER_THAN_UINT(0, count);

    for (unsigned i = 0; i < count; ++i)
    {
        TEST_ASSERT_TRUE(pixels[i].left >= 0);
        TEST_ASSERT_TRUE(pixels[i].right <= 8);
        TEST_ASSERT_TRUE(pixels[i].right > pixels[i].left);
    }

    count = 0;

    ui_text_clipped(NULL, 9, 0, "A", 42, 2, 0, 8);
    TEST_ASSERT_EQUAL_UINT(0, count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(question_pixels);
    RUN_TEST(supported_characters);
    RUN_TEST(unsupported_characters);
    RUN_TEST(number_and_text_share_pixels);
    RUN_TEST(spaces_and_case);
    RUN_TEST(horizontal_clip);

    return UNITY_END();
}
