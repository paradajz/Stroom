#pragma once

#include "milkdrop/runtime.h"
#include "ui/cd_player/controller.h"
#include "ui/settings/controller.h"

/** @brief Benchmark-owned fullscreen state, independent of player application policy. */
typedef struct
{
    PlayerState     player;
    AppSettings     settings;
    MilkdropRuntime visualizer;
    uint32_t        last_frame;
} BenchmarkRuntime;

/**
 * @brief Initialize fullscreen rendering with level meters at the high presentation rate.
 * @param runtime Benchmark state to initialize.
 */
void benchmark_runtime_init(BenchmarkRuntime* runtime);

/**
 * @brief Reset a preset and its clock for deterministic measurement.
 * @param runtime Benchmark state.
 * @param preset Compiled preset to select.
 * @param now Wrapping monotonic start time in milliseconds.
 */
void benchmark_runtime_begin(BenchmarkRuntime* runtime, PresetKind preset, uint32_t now);

/**
 * @brief Advance analysis and equations using wrapping elapsed milliseconds.
 * @param runtime Benchmark state to update.
 * @param audio Current audio analysis input.
 * @param now Wrapping monotonic milliseconds.
 */
void benchmark_runtime_step(BenchmarkRuntime* runtime, const Audio* audio, uint32_t now);
