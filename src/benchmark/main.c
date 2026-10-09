#include "benchmark/benchmark.h"
#include "benchmark/color_curve.h"
#include "profiling/benchmark.h"
#include "benchmark/runtime.h"
#include "audio/source/source.h"
#include "audio/network/network.h"
#include "platform/time/clock.h"
#include "platform/time/sleep.h"
#include "platform/platform.h"
#include "ui/frame/frame.h"
#include "ui/artwork/view.h"
#include "ui/frame/benchmark.h"
#include "ui/visualizer/scene.h"
#include <stdio.h>
#include <string.h>

#define PROFILE_ACCUMULATE(name) detail.name += benchmark_profile.name;
#define PROFILE_JSON(name)       ",\"" #name "_avg_us\":%.1f"
#define PROFILE_ARGUMENT(name)   , platform_ticks_to_us_fractional(detail->name) / result->work.count

#define MICROSECONDS_PER_SECOND       1000000u
#define BENCHMARK_IDLE_US             20000u
#define BENCHMARK_PLAYBACK_SETTLE_US  2000000u
#define BENCHMARK_PLAYBACK_TIMEOUT_US 90000000u

static uint64_t clock_us(void)
{
    return platform_ticks_to_us(platform_ticks());
}

/** @brief Finish display cleanup before leaving the benchmark. */
static void close_renderer(void)
{
    while (ui_frame_close() != 0)
    {
        platform_sleep_us(BENCHMARK_IDLE_US);
    }
}

/** @brief Finish retained worker cleanup before releasing the benchmark's devices. */
static void close_audio_sources(void)
{
    while (audio_source_close() != 0)
    {
        platform_sleep_us(BENCHMARK_IDLE_US);
    }
}

/**
 * @brief Write one completed preset measurement as a single JSON line.
 * @param events Benchmark event stream.
 * @param index Index in the compiled preset library.
 * @param elapsed_us Total time spent measuring this preset, before reporting.
 * @param result Accumulated frame measurements with a nonzero sample count.
 * @param timing Accumulated rendering profile in bus-clock ticks.
 * @param detail Accumulated detailed profile in bus-clock ticks.
 */
static void write_result(FILE* events, unsigned index, unsigned elapsed_us, const BenchmarkResult* result, const SceneTiming* timing, const BenchmarkProfile* detail)
{
    double mesh_us     = platform_ticks_to_us_fractional(timing->mesh_ticks) / result->work.count;
    double geometry_us = platform_ticks_to_us_fractional(timing->emit_ticks - timing->command_ticks) / result->work.count;
    double commands_us = platform_ticks_to_us_fractional(timing->command_ticks) / result->work.count;
    double steady_fps  = result->interval.sum ? result->interval.count * (double)MICROSECONDS_PER_SECOND / result->interval.sum : 0;

    /* Keep fields and arguments in matching order, one per line. */
    // clang-format off
    fprintf(events,
        "{\"event\":\"result\""
        ",\"index\":%u"
        ",\"elapsed_us\":%u"
        ",\"frames\":%u"
        ",\"warmup_frames\":%u"
        ",\"samples\":%u"
        ",\"first_work_us\":%u"
        ",\"preparation_avg_us\":%.1f"
        ",\"update_avg_us\":%.1f"
        ",\"render_avg_us\":%.1f"
        ",\"mesh_avg_us\":%.1f"
        ",\"feedback_geometry_avg_us\":%.1f"
        ",\"feedback_commands_avg_us\":%.1f"
        ",\"submit_avg_us\":%.1f"
        ",\"work_avg_us\":%.1f"
        ",\"work_p95_us\":%u"
        ",\"work_max_us\":%u"
        ",\"interval_avg_us\":%.1f"
        ",\"interval_p95_us\":%u"
        ",\"interval_max_us\":%u"
        ",\"steady_fps\":%.*f"
        ",\"over_budget\":%u"
        BENCHMARK_PROFILE_FIELDS(PROFILE_JSON)
        "}\n",
        index,
        elapsed_us,
        result->frames,
        result->warmup_frames,
        result->work.count,
        (unsigned)result->first_work_us,
        benchmark_mean(&result->preparation),
        benchmark_mean(&result->update),
        benchmark_mean(&result->render),
        mesh_us,
        geometry_us,
        commands_us,
        benchmark_mean(&result->submit),
        benchmark_mean(&result->work),
        (unsigned)benchmark_p95(&result->work),
        (unsigned)result->work.maximum,
        benchmark_mean(&result->interval),
        (unsigned)benchmark_p95(&result->interval),
        (unsigned)result->interval.maximum,
        MILKDROP_BENCHMARK_FPS_DECIMALS, steady_fps,
        result->over_budget
        BENCHMARK_PROFILE_FIELDS(PROFILE_ARGUMENT));
    // clang-format on
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    static Audio             audio;
    static BenchmarkRuntime  runtime;
    static AudioSourceStatus source;
    static BenchmarkResult   result;
    static SceneTiming       render_timing;
    static BenchmarkProfile  detail;

    int   platform_ready = platform_open() == 0;
    FILE* events         = fopen("host:benchmark-events.jsonl", "w");

    if (!events)
    {
        platform_close();
        return 1;
    }

    setvbuf(events, NULL, _IONBF, 0);

    if (!platform_ready)
    {
        fprintf(events, "{\"event\":\"error\",\"message\":\"scheduler initialization failed\"}\n");
        fclose(events);
        return 1;
    }

    audio_source_open(1, 0);

    if (ui_frame_open() != 0)
    {
        close_audio_sources();
        close_renderer();
        fclose(events);
        platform_close();
        return 1;
    }

    if (benchmark_verify_color_curves(ui_frame_benchmark_context(), events) != 0)
    {
        fprintf(events, "{\"event\":\"error\",\"message\":\"color curve validation failed\"}\n");
        fclose(events);
        close_audio_sources();
        /* Keep validation failures visible instead of returning silently to the browser. */

        for (;;)
        {
            const char* lines[] = { "CURVE VALIDATION FAILED", "SEE HOST REPORT" };

            ui_frame_benchmark_message(lines, sizeof(lines) / sizeof(lines[0]));
            platform_sleep_us(BENCHMARK_IDLE_US);
        }
    }

    benchmark_runtime_init(&runtime);
    ui_artwork_set_observer(network_artwork_observe);
    ui_artwork_open();
    /* Wait for real playback; silent/worker-free frames are never benchmarked. */
    uint64_t waiting      = clock_us();
    uint64_t active_since = 0;

    for (;;)
    {
        audio_source_poll(&audio, &source);

        if (audio.active && source.kind == AUDIO_SOURCE_NETWORK && !source.network_waiting)
        {
            if (!active_since)
            {
                active_since = clock_us();
            }

            if (clock_us() - active_since >= BENCHMARK_PLAYBACK_SETTLE_US)
            {
                break;
            }
        }
        else
        {
            active_since = 0;
        }

        if (clock_us() - waiting > BENCHMARK_PLAYBACK_TIMEOUT_US)
        {
            fprintf(events, "{\"event\":\"error\",\"message\":\"real network playback did not start\"}\n");
            goto closed;
        }

        platform_sleep_us(BENCHMARK_IDLE_US);
    }

    unsigned network_generation = source.network_generation;

    fprintf(events, "{\"event\":\"start\",\"version\":2,\"total\":%u,\"duration_us\":%u,\"warmup_us\":%u,\"budget_us\":%u,\"seed\":%u,\"audio\":\"generated-ariacast\",\"meters\":true,\"render_profile\":%d,\"detail_profile\":%d,\"wave_profile\":%d,\"video_mode\":\"%s\",\"refresh_hz\":%.6f}\n", MILK_PRESET_COUNT, BENCHMARK_DURATION_US, BENCHMARK_WARMUP_US, (unsigned)BENCHMARK_BUDGET_US, BENCHMARK_SEED, STROOM_BENCHMARK_PROFILE, STROOM_BENCHMARK_PROFILE, STROOM_BENCHMARK_PROFILE, DISPLAY_MODE_NAME, DISPLAY_REFRESH_HZ);

    unsigned index;

    for (index = 0; index < MILK_PRESET_COUNT; ++index)
    {
        memset(&result, 0, sizeof(result));
        memset(&render_timing, 0, sizeof(render_timing));
        memset(&detail, 0, sizeof(detail));
        benchmark_runtime_begin(&runtime, index, platform_millis());
        scene_reset();
        fprintf(events, "{\"event\":\"begin\",\"index\":%u}\n", index);

        runtime.last_frame = platform_millis();

        uint64_t started = clock_us();

        while (clock_us() - started < BENCHMARK_DURATION_US)
        {
            benchmark_profile = (BenchmarkProfile){ 0 };

            uint64_t frame_start = clock_us();
            uint32_t elapsed     = (uint32_t)(frame_start - started);

            audio_source_poll(&audio, &source);

            if (!audio.active || source.kind != AUDIO_SOURCE_NETWORK || source.network_waiting || source.network_generation != network_generation)
            {
                fprintf(events, "{\"event\":\"error\",\"message\":\"playback interrupted; run invalid\"}\n");
                goto closed;
            }

            uint32_t now      = platform_millis();
            uint64_t prepared = clock_us();

            benchmark_runtime_step(&runtime, &audio, now);

            uint64_t updated = clock_us();

            ui_frame_render(UI_SCREEN_VISUALIZER, &runtime.player, &runtime.settings, &runtime.visualizer, &audio, &source, now);

            UiFrameTiming timing    = ui_frame_timing();
            uint64_t      drawn     = platform_ticks_to_us(timing.drawn_ticks);
            uint64_t      submitted = platform_ticks_to_us(timing.submitted_ticks);
            uint64_t      presented = platform_ticks_to_us(timing.presented_ticks);

            if (elapsed >= BENCHMARK_WARMUP_US)
            {
                SceneTiming frame_timing = scene_timing();

                render_timing.mesh_ticks += frame_timing.mesh_ticks;
                render_timing.emit_ticks += frame_timing.emit_ticks;
                render_timing.command_ticks += frame_timing.command_ticks;

                BENCHMARK_PROFILE_FIELDS(PROFILE_ACCUMULATE)
            }

            benchmark_record(&result, elapsed, (uint32_t)(prepared - frame_start), (uint32_t)(updated - prepared), (uint32_t)(drawn - updated), (uint32_t)(submitted - drawn), (uint32_t)(presented - frame_start));
        }

        if (!result.work.count)
        {
            fprintf(events, "{\"event\":\"error\",\"index\":%u,\"message\":\"No measured frames for this preset\"}\n", index);
            goto closed;
        }

        write_result(events, index, (unsigned)(clock_us() - started), &result, &render_timing, &detail);
    }

    fprintf(events, "{\"event\":\"complete\",\"total\":%u}\n", MILK_PRESET_COUNT);
    /* Retain the last fullscreen image after measurement. */

    for (;;)
    {
        platform_sleep_us(BENCHMARK_IDLE_US);
    }

closed:

    while (ui_artwork_close() != 0)
    {
        platform_sleep_us(BENCHMARK_IDLE_US);
    }

    close_audio_sources();
    fclose(events);
    close_renderer();
    platform_close();

    return 0;
}
