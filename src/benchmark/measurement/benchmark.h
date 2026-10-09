#pragma once

#include "platform/graphics/display_config.h"
#include "contracts/milkdrop.h"
#include <stdint.h>

#define BENCHMARK_DURATION_US 5000000u
#define BENCHMARK_WARMUP_US   1000000u

/* Budget the baseline frame-rate mode in display refresh periods; rendering is not capped. */
#define BENCHMARK_BUDGET_US      ((uint32_t)(MILKDROP_FPS_HIGH / MILKDROP_FPS_BASELINE * DISPLAY_FRAME_US))
#define BENCHMARK_SEED           123u
#define BENCHMARK_HISTOGRAM_BINS 201

typedef struct
{
    uint64_t sum;
    uint32_t maximum;
    unsigned count;
    unsigned histogram[BENCHMARK_HISTOGRAM_BINS];
} BenchmarkTiming;

typedef struct
{
    unsigned        frames, warmup_frames, over_budget;
    uint32_t        first_work_us;
    BenchmarkTiming preparation, update, render, submit, work, interval;
} BenchmarkResult;

/**
 * @brief Record a frame, excluding the warmup window from steady-state statistics.
 * @param result Accumulated measurements to update.
 * @param elapsed_us Elapsed preset time in microseconds.
 * @param preparation Preparation time in microseconds.
 * @param update Analysis and equation time in microseconds.
 * @param render Rendering time in microseconds.
 * @param submit Command submission time in microseconds.
 * @param interval Presentation interval in microseconds.
 */
void benchmark_record(BenchmarkResult* result, uint32_t elapsed_us, uint32_t preparation, uint32_t update, uint32_t render, uint32_t submit, uint32_t interval);

/**
 * @brief Calculate mean sample duration.
 * @param timing Timing samples to inspect.
 * @return Mean microseconds, or zero when no samples were recorded.
 */
double benchmark_mean(const BenchmarkTiming* timing);

/**
 * @brief Estimate a conservative 95th percentile from one-millisecond buckets.
 * @param timing Timing samples to inspect.
 * @return Microseconds at the bucket upper bound, the measured maximum for overflow, or zero without samples.
 */
uint32_t benchmark_p95(const BenchmarkTiming* timing);
