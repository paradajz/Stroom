#include "ui/settings/controller.h"
#include "unity.h"
#include <string.h>

static Director    director;
static AppSettings settings;

void setUp(void)
{
    director_init(&director, 123);

    settings = (AppSettings){ .row = SETTING_MODE };
}

void tearDown(void)
{}

static void mode_cycles_in_both_directions(void)
{
    static const struct
    {
        DirectorMode before, previous, next;
        const char*  label;
    } modes[] = {
        { DIRECTOR_FIXED, DIRECTOR_SHUFFLE, DIRECTOR_SEQUENTIAL, "MODE: FIXED" },
        { DIRECTOR_SEQUENTIAL, DIRECTOR_FIXED, DIRECTOR_SHUFFLE, "MODE: SEQUENTIAL" },
        { DIRECTOR_SHUFFLE, DIRECTOR_SEQUENTIAL, DIRECTOR_FIXED, "MODE: SHUFFLE" }
    };

    TEST_ASSERT_EQUAL_UINT(DIRECTOR_MODE_COUNT, sizeof(modes) / sizeof(*modes));

    for (unsigned i = 0; i < sizeof(modes) / sizeof(*modes); ++i)
    {
        char label[64];

        director_set_mode(&director, modes[i].before);
        settings_format(&settings, &director, SETTING_MODE, label, sizeof(label));
        TEST_ASSERT_EQUAL_STRING(modes[i].label, label);
        settings_update(&settings, &director, INPUT_LEFT);
        TEST_ASSERT_EQUAL_INT(modes[i].previous, director.mode);
        director_set_mode(&director, modes[i].before);
        settings_update(&settings, &director, INPUT_RIGHT);
        TEST_ASSERT_EQUAL_INT(modes[i].next, director.mode);
        director_set_mode(&director, modes[i].before);
        settings_update(&settings, &director, INPUT_CROSS);
        TEST_ASSERT_EQUAL_INT(modes[i].next, director.mode);
    }
}

static void mode_count_is_not_a_valid_mode(void)
{
    DirectorMode before = director.mode;

    director_set_mode(&director, DIRECTOR_MODE_COUNT);
    TEST_ASSERT_EQUAL_INT(before, director.mode);
    director_set_mode(&director, (DirectorMode)-1);
    TEST_ASSERT_EQUAL_INT(before, director.mode);
}

static void playback_modes_and_time(void)
{
    CdPlaybackStatus cd = { .present = 1, .tracks = 3, .mode = CD_CONTINUE };
    PlayerState      player;

    player_init(&player);
    settings_open(&settings, &cd);

    settings.page = MENU_PLAYBACK;

    AudioTransportRequests actions = settings_playback_update(&settings, &player, &cd, INPUT_LEFT);

    TEST_ASSERT_EQUAL_UINT(0, actions.command_count);    // No saved program yet.

    player.saved_count     = 1;
    player.saved_tracks[0] = 2;

    const AudioTransportCommand expected[] = { AUDIO_TRANSPORT_SHUFFLE, AUDIO_TRANSPORT_REPEAT_ONE, AUDIO_TRANSPORT_REPEAT_ALL, AUDIO_TRANSPORT_PROGRAM, AUDIO_TRANSPORT_CONTINUE };

    for (unsigned i = 0; i < sizeof(expected) / sizeof(*expected); ++i)
    {
        actions = settings_playback_update(&settings, &player, &cd, INPUT_RIGHT);

        TEST_ASSERT_EQUAL_UINT(1, actions.command_count);
        TEST_ASSERT_EQUAL_INT(expected[i], actions.commands[0]);
    }

    settings.menu_row = PLAYBACK_TIME_ROW;

    settings_playback_update(&settings, &player, &cd, INPUT_CROSS);
    TEST_ASSERT_TRUE(player.time_remaining);
    settings_playback_update(&settings, &player, &cd, INPUT_LEFT);
    TEST_ASSERT_FALSE(player.time_remaining);

    settings.row = SETTING_HARD_CUTS;

    int before = director.hard_cuts;

    settings_update(&settings, &director, INPUT_CROSS);
    TEST_ASSERT_EQUAL_INT(!before, director.hard_cuts);
}

static void frame_rate_toggle(void)
{
    char label[64];

    settings.row        = SETTING_FRAME_RATE;
    settings.frame_rate = 30;

    settings_update(&settings, &director, INPUT_RIGHT);
    TEST_ASSERT_EQUAL_INT(60, settings.frame_rate);
    settings_format(&settings, &director, SETTING_FRAME_RATE, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("FRAME RATE: 60 FPS", label);
    settings_update(&settings, &director, INPUT_LEFT);
    TEST_ASSERT_EQUAL_INT(30, settings.frame_rate);
    settings_update(&settings, &director, INPUT_CROSS);
    TEST_ASSERT_EQUAL_INT(60, settings.frame_rate);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(mode_cycles_in_both_directions);
    RUN_TEST(frame_rate_toggle);
    RUN_TEST(mode_count_is_not_a_valid_mode);
    RUN_TEST(playback_modes_and_time);

    return UNITY_END();
}
