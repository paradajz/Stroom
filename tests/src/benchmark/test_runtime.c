#include "benchmark/runtime.h"
#include "contracts/milkdrop.h"
#include "unity.h"

static BenchmarkRuntime runtime;
static Audio            audio;

void setUp(void)
{
    audio = (Audio){ 0 };

    benchmark_runtime_init(&runtime);
}

void tearDown(void)
{}

static void measurements_reset_visual_state_and_ignore_playback_filter(void)
{
    TEST_ASSERT_FALSE(runtime.settings.show_panels);
    TEST_ASSERT_TRUE(runtime.settings.show_levels);
    TEST_ASSERT_EQUAL_INT(MILKDROP_FPS_HIGH, runtime.settings.frame_rate);
    benchmark_runtime_begin(&runtime, 0, 100);
    benchmark_runtime_step(&runtime, &audio, 150);

    runtime.visualizer.music.attenuated[0] = 9;

    benchmark_runtime_begin(&runtime, MILK_PRESET_COUNT - 1, 200);
    TEST_ASSERT_EQUAL_INT(MILK_PRESET_COUNT - 1, runtime.visualizer.director.current.kind);
    TEST_ASSERT_EQUAL_INT(DIRECTOR_FIXED, runtime.visualizer.director.mode);
    TEST_ASSERT_FALSE(runtime.visualizer.director.hard_cuts);
    TEST_ASSERT_EQUAL_INT(0, runtime.visualizer.director.frame_rate);
    TEST_ASSERT_EQUAL_FLOAT(0, runtime.visualizer.music.time);
    TEST_ASSERT_EQUAL_UINT(0, runtime.visualizer.music.frame);
    TEST_ASSERT_EQUAL_FLOAT(0, runtime.visualizer.music.attenuated[0]);
    benchmark_runtime_step(&runtime, &audio, 225);
    TEST_ASSERT_FLOAT_WITHIN(.00001f, .025f, runtime.visualizer.music.time);
    TEST_ASSERT_EQUAL_INT(MILK_PRESET_COUNT - 1, runtime.visualizer.director.current.kind);
}

static void elapsed_clock_wraps_and_zero_intervals_do_not_advance(void)
{
    benchmark_runtime_begin(&runtime, 0, UINT32_MAX - 9);
    benchmark_runtime_step(&runtime, &audio, UINT32_MAX - 9);
    TEST_ASSERT_EQUAL_UINT(0, runtime.visualizer.music.frame);
    benchmark_runtime_step(&runtime, &audio, 10);
    TEST_ASSERT_FLOAT_WITHIN(.00001f, .02f, runtime.visualizer.music.time);
    TEST_ASSERT_EQUAL_UINT(1, runtime.visualizer.music.frame);
    TEST_ASSERT_EQUAL_FLOAT(runtime.visualizer.music.time, runtime.visualizer.director.time);
    TEST_ASSERT_EQUAL_UINT(runtime.visualizer.music.frame, runtime.visualizer.director.frame);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(measurements_reset_visual_state_and_ignore_playback_filter);
    RUN_TEST(elapsed_clock_wraps_and_zero_intervals_do_not_advance);

    return UNITY_END();
}
