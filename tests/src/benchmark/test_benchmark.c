#include "benchmark/benchmark.h"
#include "unity.h"
#include <string.h>

void setUp(void)
{}

void tearDown(void)
{}

static void timing_boundaries(void)
{
    BenchmarkResult result = { 0 };

    benchmark_record(&result, 0, 50, 1000, 2000, 3000, 20000);
    TEST_ASSERT_EQUAL_UINT(6050, result.first_work_us);
    TEST_ASSERT_EQUAL_UINT(1, result.warmup_frames);
    TEST_ASSERT_EQUAL_UINT(0, result.work.count);
    benchmark_record(&result, BENCHMARK_WARMUP_US, 0, 1000, 9000, 10000, 20000);
    benchmark_record(&result, BENCHMARK_WARMUP_US + 1, 50, 1000, 9000, 11000, 40000);
    TEST_ASSERT_EQUAL_UINT(3, result.frames);
    TEST_ASSERT_EQUAL_UINT(0, result.over_budget);
    TEST_ASSERT_EQUAL_UINT(2, result.work.count);
    TEST_ASSERT_EQUAL_FLOAT(20525, benchmark_mean(&result.work));
    TEST_ASSERT_EQUAL_UINT(22000, benchmark_p95(&result.work));
    TEST_ASSERT_EQUAL_UINT(21050, result.work.maximum);
    TEST_ASSERT_EQUAL_FLOAT(30000, benchmark_mean(&result.interval));
    benchmark_record(&result, BENCHMARK_WARMUP_US + 2, 50, 1000, 300000, 10000, 320000);
    TEST_ASSERT_EQUAL_UINT(311050, benchmark_p95(&result.work));
    TEST_ASSERT_EQUAL_UINT(1, result.over_budget);

    BenchmarkResult boundary = { 0 };

    benchmark_record(&boundary, BENCHMARK_WARMUP_US, 0, 0, BENCHMARK_BUDGET_US, 0, BENCHMARK_BUDGET_US);
    TEST_ASSERT_EQUAL_UINT(0, boundary.over_budget);
    benchmark_record(&boundary, BENCHMARK_WARMUP_US, 0, 0, BENCHMARK_BUDGET_US + 1, 0, BENCHMARK_BUDGET_US + 1);
    TEST_ASSERT_EQUAL_UINT(1, boundary.over_budget);

    BenchmarkTiming empty = { 0 };

    TEST_ASSERT_EQUAL_FLOAT(0, benchmark_mean(&empty));
    TEST_ASSERT_EQUAL_UINT(0, benchmark_p95(&empty));
}

static void warmup_frame_can_exhaust_measurement_window(void)
{
    BenchmarkResult result = { 0 };

    benchmark_record(&result, 0, 0, 0, BENCHMARK_DURATION_US, 0, BENCHMARK_DURATION_US);
    TEST_ASSERT_EQUAL_UINT(1, result.frames);
    TEST_ASSERT_EQUAL_UINT(1, result.warmup_frames);
    TEST_ASSERT_EQUAL_UINT(0, result.work.count);
    TEST_ASSERT_EQUAL_UINT(0, result.interval.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(timing_boundaries);
    RUN_TEST(warmup_frame_can_exhaust_measurement_window);

    return UNITY_END();
}
