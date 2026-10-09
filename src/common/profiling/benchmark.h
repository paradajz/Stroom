#pragma once

#if STROOM_PRESET_BENCHMARK
#include "contracts/benchmark.h"
#include <stdint.h>
#include "platform/time/clock.h"
#endif

#if STROOM_PRESET_BENCHMARK && STROOM_BENCHMARK_PROFILE
#define PROFILE_BEGIN(name)      uint64_t name = platform_ticks()
#define PROFILE_END(field, name) (benchmark_profile.field += platform_ticks() - (name))
#define PROFILE_DRAW_BEGIN(name)                                \
    uint64_t name##_commands = benchmark_profile.draw_commands; \
    PROFILE_BEGIN(name)
#define PROFILE_DRAW_END(field, name) \
    (benchmark_profile.field += platform_ticks() - (name) - (benchmark_profile.draw_commands - name##_commands))
#define PROFILE_SPAN(field, begin, end) (benchmark_profile.field += (end) - (begin))
#else
#define PROFILE_SPAN(field, begin, end)
#define PROFILE_BEGIN(name)
#define PROFILE_END(field, name)
#define PROFILE_DRAW_BEGIN(name)
#define PROFILE_DRAW_END(field, name)
#endif

#if STROOM_PRESET_BENCHMARK
#define PROFILE_FIELD(name) uint64_t name;

/** @brief Per-frame elapsed bus ticks; geometry excludes timed drawing callbacks. */
typedef struct
{
    BENCHMARK_PROFILE_FIELDS(PROFILE_FIELD)
} BenchmarkProfile;

extern BenchmarkProfile benchmark_profile;
#endif
