#include "benchmark/runtime.h"
#include "benchmark/benchmark.h"
#include <string.h>

void benchmark_runtime_init(BenchmarkRuntime* runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    player_init(&runtime->player);

    runtime->settings.show_levels = 1;
    runtime->settings.frame_rate  = MILKDROP_FPS_HIGH;
}

void benchmark_runtime_begin(BenchmarkRuntime* runtime, PresetKind preset, uint32_t now)
{
    milkdrop_runtime_init(&runtime->visualizer, BENCHMARK_SEED);
    director_set_mode(&runtime->visualizer.director, DIRECTOR_FIXED);

    runtime->visualizer.director.hard_cuts = 0;
    runtime->last_frame                    = now;

    preset_init(&runtime->visualizer.director.current, preset, BENCHMARK_SEED);
}

void benchmark_runtime_step(BenchmarkRuntime* runtime, const Audio* audio, uint32_t now)
{
    float dt = (uint32_t)(now - runtime->last_frame) / 1000.0f;

    runtime->last_frame = now;

    milkdrop_runtime_step(&runtime->visualizer, audio, dt);
}
