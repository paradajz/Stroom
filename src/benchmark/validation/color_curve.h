#pragma once

#include <gsKit.h>
#include <stdio.h>

/** Failure codes for this API. */
typedef enum
{
    BENCHMARK_COLOR_CURVE_ERROR_MEMORY   = -1,
    BENCHMARK_COLOR_CURVE_ERROR_RENDER   = -2,
    BENCHMARK_COLOR_CURVE_ERROR_MISMATCH = -3,
} BenchmarkColorCurveError;

/**
 * @brief Validate all seven selected GS color-curve combinations before timing.
 * @param gs Initialized graphics context.
 * @param events Benchmark event stream for verification results.
 * @return Zero if no selected effect needs validation or all checks pass; a negative BenchmarkColorCurveError otherwise.
 */
int benchmark_verify_color_curves(GSGLOBAL* gs, FILE* events);
