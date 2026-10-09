#include "app/runtime.h"
#include "unity.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Substitute stroom_main in this host fixture.
 */
int stroom_main(int argc, char** argv);

static int      results[16];
static unsigned result_count, close_calls, sleeps, reports;
static uint32_t clock_ms, sleep_advance;
static char     report[256];
static int      resources_owned;

/**
 * @brief Substitute app_runtime_open in this host fixture.
 */
int app_runtime_open(AppRuntime* runtime, int argc, char** argv)
{
    (void)runtime;
    (void)argc;
    (void)argv;

    resources_owned = 1;

    return APP_RUNTIME_ERROR_RENDERER_START;
}

/**
 * @brief Substitute app_runtime_close in this host fixture.
 */
int app_runtime_close(AppRuntime* runtime)
{
    (void)runtime;
    TEST_ASSERT_TRUE(resources_owned);
    TEST_ASSERT_LESS_THAN_UINT(result_count, close_calls);

    int result = results[close_calls++];

    if (result == 0)
    {
        resources_owned = 0;
    }

    return result;
}

/**
 * @brief Substitute app_runtime_step in this host fixture.
 */
void app_runtime_step(AppRuntime* runtime)
{
    (void)runtime;
    TEST_FAIL_MESSAGE("Failed startup must not run normal frames");
}

/**
 * @brief Substitute platform_millis in this host fixture.
 */
uint32_t platform_millis(void)
{
    return clock_ms;
}

/**
 * @brief Substitute platform_sleep_us in this host fixture.
 */
void platform_sleep_us(unsigned microseconds)
{
    TEST_ASSERT_EQUAL_UINT(1000, microseconds);
    TEST_ASSERT_TRUE(resources_owned);
    ++sleeps;

    clock_ms += sleep_advance;
}

/**
 * @brief Intercept fprintf calls for fixture assertions and injected outcomes.
 */
int __wrap_fprintf(FILE* stream, const char* format, ...)
{
    TEST_ASSERT_EQUAL_PTR(stderr, stream);
    TEST_ASSERT_TRUE(resources_owned);
    ++reports;

    va_list args;

    va_start(args, format);

    int result = vsnprintf(report, sizeof(report), format, args);

    va_end(args);

    return result;
}

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    memset(results, 0, sizeof(results));
    memset(report, 0, sizeof(report));

    reports         = 0;
    sleeps          = 0;
    close_calls     = 0;
    result_count    = 0;
    clock_ms        = 0;
    sleep_advance   = 3000;
    resources_owned = 0;
}

/**
 * @brief Finish fixture cleanup and resource checks after each test.
 */
void tearDown(void)
{
    TEST_ASSERT_FALSE(resources_owned);
}

/**
 * @brief Verify quick cleanup does not report stall.
 */
static void quick_cleanup_does_not_report_stall(void)
{
    results[0]   = 1;
    results[1]   = APP_RUNTIME_ERROR_AUDIO_CLOSE;
    results[2]   = 0;
    result_count = 3;

    TEST_ASSERT_EQUAL_INT(1, stroom_main(0, NULL));
    TEST_ASSERT_EQUAL_UINT(3, close_calls);
    TEST_ASSERT_EQUAL_UINT(2, sleeps);
    TEST_ASSERT_EQUAL_UINT(0, reports);
}

/**
 * @brief Verify prolonged cleanup reports once and keeps trying.
 */
static void prolonged_cleanup_reports_once_and_keeps_trying(void)
{
    for (unsigned i = 0; i < 8; ++i)
    {
        results[i] = i % 2 ? APP_RUNTIME_ERROR_AUDIO_CLOSE : 1;
    }

    result_count = 9;
    clock_ms     = UINT32_MAX - 5000;

    TEST_ASSERT_EQUAL_INT(1, stroom_main(0, NULL));
    TEST_ASSERT_EQUAL_UINT(9, close_calls);
    TEST_ASSERT_EQUAL_UINT(8, sleeps);
    TEST_ASSERT_EQUAL_UINT(1, reports);
    TEST_ASSERT_NOT_NULL(strstr(report, "restart console"));
    TEST_ASSERT_NOT_NULL(strstr(report, "retaining resources and continuing cleanup"));
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(quick_cleanup_does_not_report_stall);
    RUN_TEST(prolonged_cleanup_reports_once_and_keeps_trying);

    return UNITY_END();
}
