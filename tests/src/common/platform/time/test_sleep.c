#include "platform/time/sleep.h"
#include <delaythread.h>
#include "unity.h"

static unsigned calls;
static uint64_t duration;

int DelayThread(int microseconds)
{
    TEST_ASSERT_GREATER_THAN_INT(0, microseconds);
    ++calls;

    duration += (unsigned)microseconds;

    return 0;
}

void setUp(void)
{
    calls    = 0;
    duration = 0;
}

void tearDown(void)
{}

/** @brief Zero returns immediately; normal delays retain their microsecond units. */
static void normal_sleep(void)
{
    platform_sleep_us(0);
    TEST_ASSERT_EQUAL_UINT(0, calls);
    platform_sleep_us(20000);
    TEST_ASSERT_EQUAL_UINT(1, calls);
    TEST_ASSERT_EQUAL_UINT64(20000, duration);
}

/** @brief Split long delays without passing negative durations to the SDK. */
static void long_sleep(void)
{
    platform_sleep_us(UINT32_MAX);
    TEST_ASSERT_EQUAL_UINT(3, calls);
    TEST_ASSERT_EQUAL_UINT64(UINT32_MAX, duration);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(normal_sleep);
    RUN_TEST(long_sleep);

    return UNITY_END();
}
