#include "platform/graphics/readback.h"
#include "platform/graphics/display_config.h"
#include "platform/memory/cache.h"
#include <screenshot.h>
#include "unity.h"

static unsigned sequence;
static int      result;
static unsigned capture_pixels[DISPLAY_WIDTH * 32];

void setUp(void)
{
    sequence = 0;
    result   = 1;
}

void tearDown(void)
{}

int ps2_screenshot(void* pixels, unsigned framebuffer, unsigned x, unsigned y, unsigned width, unsigned rows, unsigned format)
{
    TEST_ASSERT_EQUAL_UINT(0, sequence++);
    TEST_ASSERT_EQUAL_PTR(capture_pixels, pixels);
    TEST_ASSERT_EQUAL_UINT(8192 / 256, framebuffer);
    TEST_ASSERT_EQUAL_UINT(0, x);
    TEST_ASSERT_EQUAL_UINT(64, y);
    TEST_ASSERT_EQUAL_UINT(DISPLAY_WIDTH, width);
    TEST_ASSERT_EQUAL_UINT(32, rows);
    TEST_ASSERT_EQUAL_UINT(GS_PSM_CT32, format);

    return result;
}

void platform_cache_writeback(void)
{
    TEST_ASSERT_EQUAL_UINT(1, sequence++);
}

static void synchronous_readback(void)
{
    GSGLOBAL gs = { .FirstFrame = GS_SETTING_OFF };

    TEST_ASSERT_TRUE(platform_display_readback(&gs, capture_pixels, 8192, 64, 32) == 0);
    TEST_ASSERT_EQUAL_UINT(2, sequence);
    TEST_ASSERT_EQUAL_INT(GS_SETTING_ON, gs.FirstFrame);
}

static void failed_readback(void)
{
    GSGLOBAL gs = { .FirstFrame = GS_SETTING_OFF };
    result      = 0;

    TEST_ASSERT_TRUE(!(platform_display_readback(&gs, capture_pixels, 8192, 64, 32) == 0));
    TEST_ASSERT_EQUAL_UINT(2, sequence);
    TEST_ASSERT_EQUAL_INT(GS_SETTING_ON, gs.FirstFrame);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(synchronous_readback);
    RUN_TEST(failed_readback);

    return UNITY_END();
}
