#include "contracts/milkdrop.h"
#include "milkdrop/director.h"
#include "milkdrop/frame_rate.h"
#include "unity.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Verify preset transitions preserve the shared time and frame inputs.
 */
static void shared_clock(void)
{
    static MusicFeatures music;
    static Director      d;

    music_init(&music);

    music.time  = 123.5f;
    music.frame = 6175;

    director_init(&d, 7);
    preset_init_at(&d.current, (PresetKind)0, 17, music.time, music.frame);
    TEST_ASSERT_TRUE_MESSAGE(d.current.milk.inputs[0] == music.time, "d.current.milk.inputs[0] == music.time");
    director_step(&d, &music, .02f);
    director_next(&d);
    TEST_ASSERT_TRUE_MESSAGE(d.next.milk.inputs[0] == music.time, "d.next.milk.inputs[0] == music.time");
    TEST_ASSERT_TRUE_MESSAGE(d.next.milk.inputs[ML_FRAME - ML_TIME] == music.frame, "d.next.milk.inputs[ML_FRAME - ML_TIME] == music.frame");

    /* Force an imported destination to exercise both sides of a blend. */
    preset_init_at(&d.next, (PresetKind)0, 18, music.time, music.frame);

    music.time += .02f;

    ++music.frame;
    director_step(&d, &music, .02f);
    TEST_ASSERT_TRUE_MESSAGE(d.current.milk.inputs[0] == music.time && d.next.milk.inputs[0] == music.time, "d.current.milk.inputs[0] == music.time && d.next.milk.inputs[0] == music.time");
    TEST_ASSERT_TRUE_MESSAGE(d.current.milk.inputs[ML_FRAME - ML_TIME] == music.frame, "d.current.milk.inputs[ML_FRAME - ML_TIME] == music.frame");
    TEST_ASSERT_TRUE_MESSAGE(d.next.milk.inputs[ML_FRAME - ML_TIME] == music.frame, "d.next.milk.inputs[ML_FRAME - ML_TIME] == music.frame");

    d.blend_time = d.blend_duration;

    director_step(&d, &music, .02f);
    TEST_ASSERT_TRUE_MESSAGE(!d.transitioning && d.current.milk.inputs[0] == music.time, "!d.transitioning && d.current.milk.inputs[0] == music.time");
    TEST_ASSERT_EQUAL_UINT32(18, d.current.seed);
    TEST_ASSERT_EQUAL_FLOAT(0, d.elapsed);
}

/**
 * @brief Verify manual preset browsing and scheduling-mode controls.
 */
static void playback_controls(void)
{
    static Director d;
    MusicFeatures   audio = { 0 };

    director_init(&d, 123);
    TEST_ASSERT_TRUE_MESSAGE(d.mode == DIRECTOR_SHUFFLE && d.duration == 10, "d.mode == DIRECTOR_SHUFFLE && d.duration == 10");
    /* Manual browsing is ordered and wraps, even in shuffle mode. */
    director_set_mode(&d, DIRECTOR_SHUFFLE);
    preset_init(&d.current, (PresetKind)0, 123);
    director_move(&d, -1);
    TEST_ASSERT_TRUE_MESSAGE(d.next.kind == MILK_PRESET_COUNT - 1, "d.next.kind == MILK_PRESET_COUNT - 1");
    director_move(&d, 1);
    TEST_ASSERT_TRUE_MESSAGE(d.next.kind == 0, "d.next.kind == 0"); /* A press during a blend is not discarded. */

    d.blend_time = d.blend_duration;

    director_step(&d, &audio, .02f);
    TEST_ASSERT_TRUE_MESSAGE(d.current.kind == 0 && !d.transitioning, "d.current.kind == 0 && !d.transitioning");
    director_set_mode(&d, DIRECTOR_SEQUENTIAL);

    d.elapsed = d.duration + d.interval_extra - .05f;

    director_step(&d, &audio, .02f);
    TEST_ASSERT_TRUE_MESSAGE(!d.transitioning, "!d.transitioning");
    director_step(&d, &audio, .04f);
    TEST_ASSERT_TRUE_MESSAGE(d.transitioning && d.next.kind == 1, "d.transitioning && d.next.kind == 1");
    /* Changing to fixed mode stops an automatic transition and hard cuts. */
    director_set_mode(&d, DIRECTOR_FIXED);
    TEST_ASSERT_TRUE_MESSAGE(!d.transitioning && d.current.kind == 0, "!d.transitioning && d.current.kind == 0");

    d.hard_cuts       = 1;
    audio.relative[0] = audio.relative[1] = audio.relative[2] = 100;
    d.elapsed                                                 = 1000;

    director_step(&d, &audio, .1f);
    TEST_ASSERT_TRUE_MESSAGE(!d.transitioning && d.current.kind == 0, "!d.transitioning && d.current.kind == 0");
    director_adjust_duration(&d, 1);
    TEST_ASSERT_TRUE_MESSAGE(d.duration == 15 && d.elapsed == 0, "d.duration == 15 && d.elapsed == 0");
    director_move(&d, 1);

    d.blend_time = d.blend_duration;

    director_step(&d, &audio, .02f);
    TEST_ASSERT_TRUE_MESSAGE(d.current.kind == 1 && d.duration == 15 && d.mode == DIRECTOR_FIXED, "d.current.kind == 1 && d.duration == 15 && d.mode == DIRECTOR_FIXED");

    for (int i = 0; i < 100; ++i)
    {
        director_adjust_duration(&d, -1);
    }

    TEST_ASSERT_TRUE_MESSAGE(d.duration == DIRECTOR_MIN_SECONDS, "d.duration == DIRECTOR_MIN_SECONDS");

    for (int i = 0; i < 100; ++i)
    {
        director_adjust_duration(&d, 1);
    }

    TEST_ASSERT_TRUE_MESSAGE(d.duration == DIRECTOR_MAX_SECONDS, "d.duration == DIRECTOR_MAX_SECONDS");
    director_set_mode(&d, DIRECTOR_SEQUENTIAL);
    director_step(&d, &audio, .02f); /* Hard cuts use the selected order too. */
    TEST_ASSERT_TRUE_MESSAGE(d.current.kind == 2 && d.duration == DIRECTOR_MAX_SECONDS, "d.current.kind == 2 && d.duration == DIRECTOR_MAX_SECONDS");
    director_set_mode(&d, (DirectorMode)99);
    TEST_ASSERT_TRUE_MESSAGE(d.mode == DIRECTOR_SEQUENTIAL, "d.mode == DIRECTOR_SEQUENTIAL");
}

/**
 * @brief Verify variation bounds, sampling, and transition timing.
 */
static void test_interval_variation(void)
{
    static Director d;
    MusicFeatures   audio = { 0 };

    director_init(&d, 1234);
    TEST_ASSERT_TRUE_MESSAGE(d.variation == 3 && d.interval_extra >= 0 && d.interval_extra < 3, "d.variation == 3 && d.interval_extra >= 0 && d.interval_extra < 3");

    for (int i = 0; i < 20; ++i)
    {
        director_adjust_variation(&d, 1);
    }

    TEST_ASSERT_TRUE_MESSAGE(d.variation == 10, "d.variation == 10");
    director_set_mode(&d, DIRECTOR_SEQUENTIAL);

    d.duration = 5;

    int changed = 0;

    for (int i = 0; i < 8; ++i)
    {
        float extra = d.interval_extra;

        TEST_ASSERT_TRUE_MESSAGE(extra >= 0 && extra < 10, "extra >= 0 && extra < 10");

        d.elapsed = d.duration + extra - .05f;

        director_step(&d, &audio, .02f);
        TEST_ASSERT_TRUE_MESSAGE(!d.transitioning && d.interval_extra == extra, "!d.transitioning && d.interval_extra == extra");
        director_step(&d, &audio, .04f);
        TEST_ASSERT_TRUE_MESSAGE(d.transitioning, "d.transitioning");

        d.blend_time = d.blend_duration;

        director_step(&d, &audio, .01f);
        TEST_ASSERT_TRUE_MESSAGE(!d.transitioning && d.elapsed == 0, "!d.transitioning && d.elapsed == 0");

        changed |= d.interval_extra != extra;
    }

    TEST_ASSERT_TRUE_MESSAGE(changed, "changed");

    for (int i = 0; i < 20; ++i)
    {
        director_adjust_variation(&d, -1);
    }

    TEST_ASSERT_TRUE_MESSAGE(d.variation == 0 && d.interval_extra == 0 && d.elapsed == 0, "d.variation == 0 && d.interval_extra == 0 && d.elapsed == 0");

    d.elapsed = d.duration - .01f;

    director_step(&d, &audio, .02f);
    TEST_ASSERT_TRUE_MESSAGE(d.transitioning, "d.transitioning");
    director_set_mode(&d, DIRECTOR_FIXED);
    director_adjust_variation(&d, 1);

    d.elapsed = 100;

    director_step(&d, &audio, .02f);
    TEST_ASSERT_TRUE_MESSAGE(!d.transitioning, "!d.transitioning");
}

/**
 * @brief Verify slow frames advance clocks, intervals, and blends consistently.
 */
static void slow_frames(void)
{
    static Director      d;
    static MusicFeatures music;
    static Audio         audio;

    director_init(&d, 123);
    music_init(&music);
    music_step(&music, &audio, 1, 1);
    director_step(&d, &music, 1);
    TEST_ASSERT_TRUE_MESSAGE(music.time == 1 && music.frame == 1, "music.time == 1 && music.frame == 1");
    TEST_ASSERT_TRUE_MESSAGE(d.time == 1 && d.frame == 1 && d.elapsed == 1, "d.time == 1 && d.frame == 1 && d.elapsed == 1");
    TEST_ASSERT_TRUE_MESSAGE(d.current.milk.inputs[ML_TIME - ML_TIME] == 1, "d.current.milk.inputs[ML_TIME - ML_TIME] == 1");
    TEST_ASSERT_TRUE_MESSAGE(d.current.milk.inputs[ML_FPS - ML_TIME] == 1, "d.current.milk.inputs[ML_FPS - ML_TIME] == 1");
    /* A blend already in progress can finish during a slow frame. */
    director_next(&d);

    d.blend_duration = 0.5f;

    PresetKind next = d.next.kind;

    music_step(&music, &audio, 1, 1);
    director_step(&d, &music, 1);
    TEST_ASSERT_TRUE_MESSAGE(!d.transitioning && d.current.kind == next, "!d.transitioning && d.current.kind == next");
    TEST_ASSERT_TRUE_MESSAGE(d.elapsed == 0 && d.time == 2 && d.frame == 2, "d.elapsed == 0 && d.time == 2 && d.frame == 2");
    TEST_ASSERT_TRUE_MESSAGE(d.current.milk.inputs[ML_TIME - ML_TIME] == 2, "d.current.milk.inputs[ML_TIME - ML_TIME] == 2");
    TEST_ASSERT_TRUE_MESSAGE(d.current.milk.inputs[ML_FPS - ML_TIME] == 1, "d.current.milk.inputs[ML_FPS - ML_TIME] == 1");
    /* Partial blend progress uses elapsed time rather than a 100 ms cap. */
    director_next(&d);

    d.blend_duration = 2;

    music_step(&music, &audio, 0.5f, 1);
    director_step(&d, &music, 0.5f);
    TEST_ASSERT_TRUE_MESSAGE(d.transitioning && d.blend_time == 0.5f, "d.transitioning && d.blend_time == 0.5f");
    TEST_ASSERT_TRUE_MESSAGE(d.current.milk.inputs[ML_TIME - ML_TIME] == 2.5f && d.next.milk.inputs[ML_TIME - ML_TIME] == 2.5f, "d.current.milk.inputs[ML_TIME - ML_TIME] == 2.5f && d.next.milk.inputs[ML_TIME - ML_TIME] == 2.5f");
    TEST_ASSERT_TRUE_MESSAGE(director_progress(&d) == 0.25f, "director_progress(&d) == 0.25f");
    TEST_ASSERT_TRUE_MESSAGE(d.current.milk.inputs[ML_FPS - ML_TIME] == 2, "d.current.milk.inputs[ML_FPS - ML_TIME] == 2");
    TEST_ASSERT_TRUE_MESSAGE(d.next.milk.inputs[ML_FPS - ML_TIME] == 2, "d.next.milk.inputs[ML_FPS - ML_TIME] == 2");
    /* Crossing the interval starts one transition without replaying frames. */
    director_init(&d, 123);
    music_init(&music);

    d.variation      = 0;
    d.interval_extra = 0;

    music_step(&music, &audio, 9.5f, 1);
    director_step(&d, &music, 9.5f);
    TEST_ASSERT_TRUE_MESSAGE(!d.transitioning && d.elapsed == 9.5f, "!d.transitioning && d.elapsed == 9.5f");
    music_step(&music, &audio, 1, 1);
    director_step(&d, &music, 1);
    TEST_ASSERT_TRUE_MESSAGE(d.transitioning && d.elapsed == 10.5f, "d.transitioning && d.elapsed == 10.5f");
    TEST_ASSERT_TRUE_MESSAGE(d.frame == 2 && d.blend_time == 0, "d.frame == 2 && d.blend_time == 0");
    TEST_ASSERT_EQUAL_FLOAT(10.5f, d.next.milk.inputs[ML_TIME - ML_TIME]);
    TEST_ASSERT_EQUAL_FLOAT(2, d.next.milk.inputs[ML_FRAME - ML_TIME]);
}

/** Finish one transition without waiting for a preset interval. */
static void finish_transition(Director* d)
{
    MusicFeatures audio = { 0 };

    d->blend_time = d->blend_duration;

    director_step(d, &audio, .001f);
    TEST_ASSERT_FALSE(d->transitioning);
}

/** Each shuffled cycle visits the entire library once, including startup. */
static void shuffled_cycles(void)
{
    static Director d;

    director_init(&d, 87);

    PresetKind previous   = d.current.kind;
    unsigned   reshuffled = 0;
    PresetKind last_order[MILK_PRESET_COUNT];

    memcpy(last_order, d.order, sizeof(last_order));

    for (unsigned cycle = 0; cycle < 12; ++cycle)
    {
        unsigned seen[MILK_PRESET_COUNT] = { 0 };

        if (cycle)
        {
            director_next(&d);
            finish_transition(&d);
            TEST_ASSERT_NOT_EQUAL(previous, d.current.kind);

            reshuffled |= memcmp(last_order, d.order, sizeof(last_order)) != 0;
        }

        memcpy(last_order, d.order, sizeof(last_order));

        for (unsigned slot = 0; slot < MILK_PRESET_COUNT; ++slot)
        {
            if (slot)
            {
                director_next(&d);

                unsigned   cursor  = d.order_at;
                PresetKind pending = d.next.kind;

                director_next(&d);
                TEST_ASSERT_EQUAL_UINT(cursor, d.order_at);
                TEST_ASSERT_EQUAL_INT(pending, d.next.kind);
                finish_transition(&d);
            }

            TEST_ASSERT_LESS_THAN_UINT(MILK_PRESET_COUNT, d.current.kind);
            TEST_ASSERT_FALSE(seen[d.current.kind]);

            seen[d.current.kind] = 1;

            TEST_ASSERT_EQUAL_INT(last_order[slot], d.current.kind);
            TEST_ASSERT_EQUAL_UINT(slot + 1, d.order_at);
            TEST_ASSERT_EQUAL_MEMORY(last_order, d.order, sizeof(last_order));

            previous = d.current.kind;
        }

        for (unsigned i = 0; i < MILK_PRESET_COUNT; ++i)
        {
            TEST_ASSERT_TRUE(seen[i]);
        }
    }

    TEST_ASSERT_TRUE(reshuffled);
}

/** Mode and manual selections anchor a new cycle; timing changes preserve it. */
static void shuffle_controls(void)
{
    static Director d;

    director_init(&d, 144);
    director_set_mode(&d, DIRECTOR_SEQUENTIAL);
    director_next(&d);

    PresetKind pending = d.next.kind;

    director_set_mode(&d, DIRECTOR_SHUFFLE);
    TEST_ASSERT_EQUAL_INT(pending, d.order[0]);
    TEST_ASSERT_EQUAL_UINT(1, d.order_at);
    finish_transition(&d);
    director_next(&d);
    director_set_mode(&d, DIRECTOR_FIXED);
    TEST_ASSERT_FALSE(d.transitioning);
    director_set_mode(&d, DIRECTOR_SHUFFLE);
    TEST_ASSERT_EQUAL_INT(d.current.kind, d.order[0]);
    TEST_ASSERT_EQUAL_UINT(1, d.order_at);

    PresetKind saved[MILK_PRESET_COUNT];

    memcpy(saved, d.order, sizeof(saved));
    director_set_mode(&d, DIRECTOR_SHUFFLE);
    director_adjust_duration(&d, 1);
    director_adjust_variation(&d, 1);
    TEST_ASSERT_EQUAL_MEMORY(saved, d.order, sizeof(saved));
    TEST_ASSERT_EQUAL_UINT(1, d.order_at);

    for (unsigned i = 0; i < 2; ++i)
    {
        PresetKind from = d.transitioning ? d.next.kind : d.current.kind;

        director_move(&d, 1);
        TEST_ASSERT_EQUAL_INT((from + 1) % MILK_PRESET_COUNT, d.next.kind);
        TEST_ASSERT_EQUAL_INT(d.next.kind, d.order[0]);
        TEST_ASSERT_EQUAL_UINT(1, d.order_at);
    }

    finish_transition(&d);

    unsigned seen[MILK_PRESET_COUNT] = { 0 };

    seen[d.current.kind] = 1;

    for (unsigned i = 1; i < MILK_PRESET_COUNT; ++i)
    {
        director_next(&d);
        finish_transition(&d);
        TEST_ASSERT_FALSE(seen[d.current.kind]);

        seen[d.current.kind] = 1;
    }

    TEST_ASSERT_EQUAL_STRING("SHUFFLE", director_mode_name(DIRECTOR_SHUFFLE));
}

/** Audio-triggered cuts consume the same order without skipping or repeating entries. */
static void shuffled_hard_cuts(void)
{
    static Director d;
    MusicFeatures   audio = { 0 };

    audio.relative[0] = audio.relative[1] = audio.relative[2] = 6;

    director_init(&d, 33);

    d.hard_cuts = 1;

    unsigned seen[MILK_PRESET_COUNT] = { 0 };

    seen[d.current.kind] = 1;

    for (unsigned i = 1; i < MILK_PRESET_COUNT; ++i)
    {
        d.hard_cut_threshold = 5;

        director_step(&d, &audio, .02f);
        TEST_ASSERT_FALSE(d.transitioning);
        TEST_ASSERT_FALSE(seen[d.current.kind]);

        seen[d.current.kind] = 1;

        TEST_ASSERT_EQUAL_INT(d.order[i], d.current.kind);
        TEST_ASSERT_EQUAL_UINT(i + 1, d.order_at);
    }
}

/** Startup uses the seed for the shuffled order and default interval variation. */
static void startup_randomness(void)
{
    static Director first, repeat;
    unsigned        seen[MILK_PRESET_COUNT] = { 0 };
    unsigned        distinct                = 0;
    int             varied_delay            = 0;

    for (uint32_t seed = 0; seed < 64; ++seed)
    {
        director_init(&first, seed);
        director_init(&repeat, seed);
        TEST_ASSERT_TRUE_MESSAGE(!memcmp(&first, &repeat, sizeof(first)), "!memcmp(&first, &repeat, sizeof(first))");
        TEST_ASSERT_TRUE_MESSAGE(first.mode == DIRECTOR_SHUFFLE && first.duration == 10 && first.variation == 3, "first.mode == DIRECTOR_SHUFFLE && first.duration == 10 && first.variation == 3");
        TEST_ASSERT_TRUE_MESSAGE(first.current.kind < MILK_PRESET_COUNT && !first.transitioning && first.elapsed == 0, "first.current.kind < MILK_PRESET_COUNT && !first.transitioning && first.elapsed == 0");
        TEST_ASSERT_TRUE_MESSAGE(first.interval_extra >= 0 && first.interval_extra < 3, "first.interval_extra >= 0 && first.interval_extra < 3");

        varied_delay |= first.interval_extra > 0;

        if (!seen[first.current.kind])
        {
            seen[first.current.kind] = 1;

            ++distinct;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(MILK_PRESET_COUNT == 1 || distinct > 1, "MILK_PRESET_COUNT == 1 || distinct > 1");
    TEST_ASSERT_TRUE_MESSAGE(varied_delay, "varied_delay");
}

/**
 * @brief Run regression checks for preset rendering and scheduling.
 *
 */
static void preset_regressions(void)
{
    MusicFeatures a;

    music_init(&a);

    for (unsigned kind = 0; kind < MILK_PRESET_COUNT; ++kind)
    {
        TEST_ASSERT_TRUE_MESSAGE(!strcmp(preset_name(kind), milk_programs[kind].name), "!strcmp(preset_name(kind), milk_programs[kind].name)");
    }

    TEST_ASSERT_TRUE_MESSAGE(!strcmp(preset_name(MILK_PRESET_COUNT), "UNKNOWN"), "!strcmp(preset_name(MILK_PRESET_COUNT), \"UNKNOWN\")");

    Director d, t;

    director_init(&d, 188);
    director_init(&t, 188);

    d.mode = t.mode = DIRECTOR_SHUFFLE;

    unsigned seen[MILK_PRESET_COUNT] = { 0 }, changes = 0;

    seen[d.current.kind] = 1;

    for (unsigned i = 0; i < 60000; ++i)
    {
        int        kind              = d.current.kind;
        int        was_transitioning = d.transitioning;
        PresetKind destination       = d.next.kind;

        director_step(&d, &a, .02f);
        director_step(&t, &a, .02f);
        TEST_ASSERT_TRUE_MESSAGE(!memcmp(&d, &t, sizeof(d)), "!memcmp(&d, &t, sizeof(d))");
        TEST_ASSERT_TRUE_MESSAGE(director_mix(&d) >= 0 && director_mix(&d) <= 1, "director_mix(&d) >= 0 && director_mix(&d) <= 1");

        if (was_transitioning && !d.transitioning)
        {
            TEST_ASSERT_EQUAL_INT(destination, d.current.kind);
            TEST_ASSERT_EQUAL_FLOAT(0, d.elapsed);
            TEST_ASSERT_TRUE_MESSAGE((int)d.current.kind != kind, "(int)d.current.kind != kind && (int)d.current.kind != previous");

            seen[d.current.kind] = 1;

            ++changes;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(changes > 80, "changes > 80");
    /* A larger library cannot be expected to appear in a fixed 20-minute run.
     * Exercise selection separately, without simulating hours of audio. */

    for (unsigned i = 0; i < MILK_PRESET_COUNT * 32; ++i)
    {
        director_next(&d);
        director_next(&t);

        d.blend_time = d.blend_duration;
        t.blend_time = t.blend_duration;

        director_step(&d, &a, .02f);
        director_step(&t, &a, .02f);
        TEST_ASSERT_TRUE_MESSAGE(!memcmp(&d, &t, sizeof(d)), "!memcmp(&d, &t, sizeof(d))");

        seen[d.current.kind] = 1;
    }

    for (unsigned i = 0; i < MILK_PRESET_COUNT; ++i)
    {
        TEST_ASSERT_TRUE_MESSAGE(seen[i], "seen[i]");
    }

    director_init(&d, 3);

    PresetKind first = d.current.kind;

    TEST_ASSERT_TRUE_MESSAGE(d.mode == DIRECTOR_SHUFFLE, "d.mode == DIRECTOR_SHUFFLE");
    director_set_mode(&d, DIRECTOR_FIXED);

    for (unsigned i = 0; i < 3000; ++i)
    {
        director_step(&d, &a, .02f);
    }

    TEST_ASSERT_TRUE_MESSAGE(d.current.kind == first && !d.transitioning && d.elapsed > 50, "d.current.kind == first && !d.transitioning && d.elapsed > 50");
    director_next(&d);
    TEST_ASSERT_TRUE_MESSAGE(d.transitioning, "d.transitioning");

    for (unsigned i = 0; i < 100; ++i)
    {
        director_step(&d, &a, .02f);
    }

    TEST_ASSERT_TRUE_MESSAGE(!d.transitioning && d.current.kind != first && d.mode == DIRECTOR_FIXED, "!d.transitioning && d.current.kind != first && d.mode == DIRECTOR_FIXED");

    Director before = d;

    director_step(&d, &a, NAN);
    TEST_ASSERT_TRUE_MESSAGE(!memcmp(&before, &d, sizeof(d)), "!memcmp(&before, &d, sizeof(d))");
    puts("PASS: MilkDrop-only library, shared clock, interval variation, deterministic shuffled cycles, manual hold/advance");
}

/**
 * @brief Fixtures are initialized by each scenario.
 */
void setUp(void)
{}

/**
 * @brief This suite owns no external resources.
 */
void tearDown(void)
{}

/**
 * @brief Run the regression scenario.
 * @return Number of failed Unity cases.
 */
static void frame_rate_filters_all_selection_paths(void)
{
    static Director d;

    director_init(&d, 123);

    unsigned   expected = 0;
    PresetKind slow     = 0;

    for (unsigned i = 0; i < MILK_PRESET_COUNT; ++i)
    {
        expected += milk_programs[i].max_frame_rate >= MILKDROP_FPS_HIGH;

        if (milk_programs[i].max_frame_rate < MILKDROP_FPS_HIGH)
        {
            slow = i;
        }
    }

    TEST_ASSERT_TRUE(expected > 1 && expected < MILK_PRESET_COUNT);
    preset_init(&d.current, slow, 123);
    director_next(&d);
    TEST_ASSERT_TRUE(d.transitioning);
    TEST_ASSERT_TRUE(director_set_frame_rate(&d, MILKDROP_FPS_HIGH) == 0);
    TEST_ASSERT_FALSE(d.transitioning);
    TEST_ASSERT_TRUE(milk_programs[d.current.kind].max_frame_rate >= MILKDROP_FPS_HIGH);
    TEST_ASSERT_EQUAL_UINT(expected, d.order_count);

    for (unsigned i = 0; i < d.order_count; ++i)
    {
        TEST_ASSERT_TRUE(milk_programs[d.order[i]].max_frame_rate >= MILKDROP_FPS_HIGH);

        for (unsigned j = 0; j < i; ++j)
        {
            TEST_ASSERT_NOT_EQUAL(d.order[j], d.order[i]);
        }
    }

    for (int mode = DIRECTOR_FIXED; mode < DIRECTOR_MODE_COUNT; ++mode)
    {
        director_set_mode(&d, mode);

        for (unsigned i = 0; i < MILK_PRESET_COUNT * 2; ++i)
        {
            director_next(&d);
            TEST_ASSERT_TRUE(milk_programs[d.next.kind].max_frame_rate >= MILKDROP_FPS_HIGH);

            d.current       = d.next;
            d.transitioning = 0;
        }

        for (int direction = -1; direction <= 1; direction += 2)
        {
            for (unsigned i = 0; i < MILK_PRESET_COUNT; ++i)
            {
                director_move(&d, direction);
                TEST_ASSERT_TRUE(milk_programs[d.next.kind].max_frame_rate >= MILKDROP_FPS_HIGH);
            }
        }
    }

    TEST_ASSERT_TRUE(director_set_frame_rate(&d, MILKDROP_FPS_BASELINE) == 0);
    TEST_ASSERT_EQUAL_UINT(MILK_PRESET_COUNT, d.order_count);
    TEST_ASSERT_TRUE(!(director_set_frame_rate(&d, 45) == 0));
    TEST_ASSERT_EQUAL_INT(MILKDROP_FPS_BASELINE, d.frame_rate);
}

/** @brief Every approved playback preset must meet the saved playback FPS baseline. */
static void playback_presets_meet_baseline(void)
{
    for (unsigned i = 0; i < MILK_PRESET_COUNT; ++i)
    {
        TEST_ASSERT_TRUE_MESSAGE(milk_programs[i].max_frame_rate >= MILKDROP_FPS_BASELINE, milk_programs[i].name);
    }
}

/** @brief Verify build-time classifications at the configured 480p boundaries. */
static void frame_rate_uses_display_refresh(void)
{
    /* Independent score/precision fixtures; all classifications are C constants. */
    static const int rates[] = {
        MILK_FRAME_RATE_MAXIMUM(29.97f),
        MILK_FRAME_RATE_MAXIMUM(29.96f),
        MILK_FRAME_RATE_MAXIMUM(29.93f),
        MILK_FRAME_RATE_MAXIMUM(59.94f),
        MILK_FRAME_RATE_MAXIMUM(59.93f),
        MILK_FRAME_RATE_MAXIMUM(0)
    };

    TEST_ASSERT_EQUAL_INT(MILKDROP_FPS_BASELINE, rates[0]);
    TEST_ASSERT_EQUAL_INT(0, rates[1]);
    TEST_ASSERT_EQUAL_INT(0, rates[2]);
    TEST_ASSERT_EQUAL_INT(MILKDROP_FPS_HIGH, rates[3]);
    TEST_ASSERT_EQUAL_INT(MILKDROP_FPS_BASELINE, rates[4]);
    TEST_ASSERT_EQUAL_INT(0, rates[5]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(playback_presets_meet_baseline);
    RUN_TEST(frame_rate_uses_display_refresh);
    RUN_TEST(startup_randomness);
    RUN_TEST(frame_rate_filters_all_selection_paths);
    RUN_TEST(shuffled_cycles);
    RUN_TEST(shuffle_controls);
    RUN_TEST(shuffled_hard_cuts);
    RUN_TEST(slow_frames);
    RUN_TEST(test_interval_variation);
    RUN_TEST(playback_controls);
    RUN_TEST(shared_clock);
    RUN_TEST(preset_regressions);

    return UNITY_END();
}
