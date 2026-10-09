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

/* Before timing, verify the GS curve against an RGB test image when the
 * selection contains brighten, darken or solarize. All seven combinations are checked. A failing effect must not produce valid scores. */
int benchmark_verify_color_curves(GSGLOBAL* gs, FILE* events);
