#include "platform/time/clock.h"
#include <timer.h>
#include "unity.h"

static uint64_t now;

/**
 * @brief Substitute GetTimerSystemTime in this host fixture.
 */
uint64_t GetTimerSystemTime(void)
{
    return now;
}

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    now = 0;
}

/**
 * @brief No per-test cleanup is required.
 */
void tearDown(void)
{}

/**
 * @brief Verify timestamp conversion.
 */
static void timestamp_conversion(void)
{
    now = (uint64_t)kBUSCLK * 3;

    TEST_ASSERT_EQUAL_UINT64(now, platform_ticks());
    TEST_ASSERT_EQUAL_UINT32(3000, platform_millis());
    TEST_ASSERT_EQUAL_UINT64(3000000, platform_ticks_to_us(now));
    TEST_ASSERT_EQUAL_UINT64(0, platform_ticks_to_us(147));
    TEST_ASSERT_EQUAL_UINT64(1, platform_ticks_to_us(148));
}

/**
 * @brief Verify fractional profile conversion.
 */
static void fractional_profile_conversion(void)
{
    double half_millisecond = platform_ticks_to_us_fractional(73728);
    double fractional       = platform_ticks_to_us_fractional(148);

    TEST_ASSERT_TRUE(half_millisecond > 499.999999 && half_millisecond < 500.000001);
    TEST_ASSERT_TRUE(fractional > 1.003688 && fractional < 1.003690);
}

/**
 * @brief Verify long timestamp conversion.
 */
static void long_timestamp_conversion(void)
{
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(125099989649180444), platform_ticks_to_us(UINT64_MAX));
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(timestamp_conversion);
    RUN_TEST(fractional_profile_conversion);
    RUN_TEST(long_timestamp_conversion);

    return UNITY_END();
}
