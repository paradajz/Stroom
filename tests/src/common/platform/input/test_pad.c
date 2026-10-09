#include "platform/input/pad.h"
#include "platform/iop/modules.h"
#include "unity.h"
#include <libpad.h>

static int      close_result;
static unsigned close_calls;
static unsigned held_buttons;

int padPortClose(int port, int slot)
{
    TEST_ASSERT_EQUAL_INT(0, port);
    TEST_ASSERT_EQUAL_INT(0, slot);
    ++close_calls;

    return close_result;
}

int platform_iop_module(const Ps2IopModule* module, char* error, size_t capacity)
{
    (void)module;
    (void)error;
    (void)capacity;

    return 0;
}

int platform_iop_probe(unsigned id)
{
    (void)id;

    return 1;
}

int scr_printf(const char* format, ...)
{
    (void)format;

    return 0;
}

int padInit(int mode)
{
    (void)mode;

    return 1;
}

int padPortOpen(int port, int slot, void* padArea)
{
    (void)port;
    (void)slot;
    (void)padArea;

    return 1;
}

int padGetState(int port, int slot)
{
    (void)port;
    (void)slot;

    return PAD_STATE_STABLE;
}

unsigned char padRead(int port, int slot, struct padButtonStatus* data)
{
    (void)port;
    (void)slot;

    data->btns = (unsigned short)(held_buttons ^ 0xffffu);

    return 1;
}

void setUp(void)
{
    close_result = 1;
    close_calls  = 0;
    held_buttons = 0;
}

void tearDown(void)
{}

static void disabled_close_skips_sdk(void)
{
    TEST_ASSERT_TRUE(platform_pad_close(0) == 0);
    TEST_ASSERT_EQUAL_UINT(0, close_calls);
}

static void failed_close_can_be_retried(void)
{
    TEST_ASSERT_TRUE(platform_pad_open() == 0);

    close_result = -7;

    TEST_ASSERT_TRUE(!(platform_pad_close(1) == 0));

    close_result = 0;

    TEST_ASSERT_TRUE(!(platform_pad_close(1) == 0));

    close_result = 1;

    TEST_ASSERT_TRUE(platform_pad_close(1) == 0);
    TEST_ASSERT_EQUAL_UINT(3, close_calls);
}

static void reopening_resets_press_history(void)
{
    TEST_ASSERT_TRUE(platform_pad_open() == 0);

    held_buttons     = PAD_START;
    InputState input = platform_pad_read(1);

    TEST_ASSERT_EQUAL_UINT(INPUT_START, input.pressed);

    input = platform_pad_read(1);

    TEST_ASSERT_EQUAL_UINT(INPUT_START, input.held);
    TEST_ASSERT_EQUAL_UINT(0, input.pressed);
    TEST_ASSERT_TRUE(platform_pad_close(1) == 0);
    TEST_ASSERT_TRUE(platform_pad_open() == 0);

    input = platform_pad_read(1);

    TEST_ASSERT_EQUAL_UINT(INPUT_START, input.held);
    TEST_ASSERT_EQUAL_UINT(INPUT_START, input.pressed);
    TEST_ASSERT_TRUE(platform_pad_close(1) == 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(disabled_close_skips_sdk);
    RUN_TEST(failed_close_can_be_retried);
    RUN_TEST(reopening_resets_press_history);

    return UNITY_END();
}
