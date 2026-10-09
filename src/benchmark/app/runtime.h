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

/** @brief Initialize fullscreen rendering with level meters and the high presentation rate. */
void benchmark_runtime_init(BenchmarkRuntime* runtime);

/** @brief Reset preset state and its clock for a deterministic measurement. */
void benchmark_runtime_begin(BenchmarkRuntime* runtime, PresetKind preset, uint32_t now);

/** @brief Advance shared analysis and equations using wrapping elapsed milliseconds. */
void benchmark_runtime_step(BenchmarkRuntime* runtime, const Audio* audio, uint32_t now);
