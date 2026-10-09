#include "app/controller.h"
#include "unity.h"
#include <string.h>

static AppConfig         config;
static AppState          app;
static Audio             audio;
static AudioSourceStatus source;
static uint32_t          now;

void setUp(void)
{
    memset(&audio, 0, sizeof(audio));
    memset(&source, 0, sizeof(source));

    source.kind = AUDIO_SOURCE_CD;
    source.cd   = (CdPlaybackStatus){ .tracks = 17, .track = 1, .present = 1, .generation = 1 };
    now         = 0;
    config      = app_config_defaults();

    app_init(&app, 123, now, &config);
    app_update(&app, &audio, &source, (InputState){ .connected = 1 }, now);
}

void tearDown(void)
{}

static AppActions input(unsigned pressed, unsigned held, int connected)
{
    now += 20;

    return app_update(&app, &audio, &source, (InputState){ held, pressed, connected }, now);
}

static AppActions press(unsigned bits)
{
    now += 20;

    return app_update(&app, &audio, &source, (InputState){ bits, bits, 1 }, now);
}

static void open_playback(void)
{
    press(INPUT_SELECT);
    press(INPUT_DOWN);
    press(INPUT_CROSS);
    TEST_ASSERT_EQUAL_INT(MENU_PLAYBACK, app.settings.page);
}

static void global_transport_and_menu(void)
{
    for (int visible = 0; visible <= 1; ++visible)
    {
        app.settings.show_panels = visible;

        for (int menu = 0; menu <= 1; ++menu)
        {
            app.settings.open = menu;

            AppActions a = press(INPUT_START);

            TEST_ASSERT_EQUAL_UINT(1, a.transport.command_count);
            TEST_ASSERT_EQUAL_INT(AUDIO_TRANSPORT_PLAY_PAUSE, a.transport.commands[0]);
            TEST_ASSERT_EQUAL_INT(menu, app.settings.open);
            TEST_ASSERT_EQUAL_INT(visible, app.settings.show_panels);
        }

        press(INPUT_SELECT);
        TEST_ASSERT_FALSE(app.settings.open);
        press(INPUT_SELECT);
        TEST_ASSERT_TRUE(app.settings.open);
        TEST_ASSERT_EQUAL_INT(MENU_ROOT, app.settings.page);
    }
}

static void program_returns_to_menu_and_preserves_saved_tracks(void)
{
    app.settings.show_panels = 0;

    open_playback();
    press(INPUT_DOWN);
    press(INPUT_CROSS);
    TEST_ASSERT_TRUE(app.player.editing);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_CD_PLAYER, app_screen(&app, &source));
    press(INPUT_CROSS);

    AppActions a = press(INPUT_START);

    TEST_ASSERT_EQUAL_INT(AUDIO_TRANSPORT_PLAY_PAUSE, a.transport.commands[0]);
    TEST_ASSERT_TRUE(app.player.editing);

    a = press(INPUT_SQUARE);

    TEST_ASSERT_EQUAL_UINT(1, a.transport.program_count);
    TEST_ASSERT_EQUAL_INT(1, a.transport.program[0]);
    TEST_ASSERT_FALSE(app.player.editing);
    TEST_ASSERT_TRUE(app.settings.open);
    TEST_ASSERT_EQUAL_INT(MENU_PLAYBACK, app.settings.page);
    TEST_ASSERT_EQUAL_INT(4, app.settings.playback_mode);
    press(INPUT_UP);

    a = press(INPUT_RIGHT);    // Program -> Continue

    TEST_ASSERT_EQUAL_INT(AUDIO_TRANSPORT_CONTINUE, a.transport.commands[0]);

    a = press(INPUT_LEFT);    // Continue -> saved Program

    TEST_ASSERT_EQUAL_INT(AUDIO_TRANSPORT_PROGRAM, a.transport.commands[0]);
    press(INPUT_DOWN);
    press(INPUT_CROSS);
    TEST_ASSERT_EQUAL_INT(1, app.player.count);
    press(INPUT_RIGHT);
    press(INPUT_CROSS);
    TEST_ASSERT_EQUAL_INT(2, app.player.count);

    a = press(INPUT_TRIANGLE);

    TEST_ASSERT_EQUAL_UINT(0, a.transport.program_count);
    TEST_ASSERT_TRUE(app.settings.open);
    press(INPUT_CROSS);
    TEST_ASSERT_EQUAL_INT(1, app.player.count);
    press(INPUT_SELECT);
    TEST_ASSERT_FALSE(app.player.editing);
    TEST_ASSERT_TRUE(app.settings.open);
}

static void menus_and_network_ownership(void)
{
    press(INPUT_SELECT);
    press(INPUT_CROSS);
    TEST_ASSERT_EQUAL_INT(MENU_VISUALIZER, app.settings.page);
    TEST_ASSERT_EQUAL_INT(SETTING_DISPLAY, app.settings.row);

    for (int row = SETTING_DISPLAY; row < SETTING_PRESET_NAME; ++row)
    {
        press(INPUT_DOWN);
    }

    press(INPUT_CROSS);
    TEST_ASSERT_TRUE(app.settings.show_name);
    press(INPUT_TRIANGLE);
    press(INPUT_R2);
    TEST_ASSERT_FALSE(app.settings.show_panels);
    TEST_ASSERT_FALSE(app.settings.open);

    source.kind = AUDIO_SOURCE_NETWORK;

    AppActions a = press(INPUT_START);

    TEST_ASSERT_EQUAL_UINT(0, a.transport.command_count);
    TEST_ASSERT_FALSE(app.settings.open);
    press(INPUT_SELECT);
    press(INPUT_DOWN);
    press(INPUT_CROSS);
    TEST_ASSERT_EQUAL_INT(MENU_PLAYBACK, app.settings.page);
}

static void disc_change_resets_program(void)
{
    app.player.saved_count = 1;
    app.player.editing     = 1;
    app.settings.open      = 1;

    ++source.cd.generation;
    press(0);
    TEST_ASSERT_EQUAL_INT(0, app.player.saved_count);
    TEST_ASSERT_FALSE(app.player.editing);
    TEST_ASSERT_FALSE(app.settings.open);
}

static void listening_intro_and_disconnected_input(void)
{
    source.kind               = AUDIO_SOURCE_NETWORK;
    source.listening          = 1;
    source.network_generation = 1;

    press(0);
    TEST_ASSERT_TRUE(app.settings.show_panels);

    now += 5000;

    press(0);
    TEST_ASSERT_FALSE(app.settings.show_panels);
    app_update(&app, &audio, &source, (InputState){ INPUT_SELECT, INPUT_SELECT, 0 }, now + 20);
    TEST_ASSERT_FALSE(app.settings.open);
    press(INPUT_SELECT);
    TEST_ASSERT_TRUE(app.settings.open);
    TEST_ASSERT_FALSE(app.settings.show_panels);
}

static void cross_does_not_interrupt_visualization(void)
{
    for (int network = 0; network < 2; ++network)
    {
        source.kind = network ? AUDIO_SOURCE_NETWORK : AUDIO_SOURCE_CD;

        press(0);

        app.settings.show_panels = 0;

        director_set_mode(&app.visualizer.director, DIRECTOR_FIXED);

        for (unsigned attempt = 0; attempt < 2; ++attempt)
        {
            float      time    = app.visualizer.music.time;
            unsigned   frame   = app.visualizer.music.frame;
            PresetKind kind    = app.visualizer.director.current.kind;
            AppActions actions = press(INPUT_CROSS);

            TEST_ASSERT_EQUAL_INT(UI_SCREEN_VISUALIZER, app_screen(&app, &source));
            TEST_ASSERT_EQUAL_UINT(0, actions.transport.command_count);
            TEST_ASSERT_FALSE(actions.reset_scene);
            TEST_ASSERT_TRUE(app.visualizer.feedback);
            TEST_ASSERT_FLOAT_WITHIN(.00001f, time + .02f, app.visualizer.music.time);
            TEST_ASSERT_EQUAL_UINT(frame + 1, app.visualizer.music.frame);
            TEST_ASSERT_EQUAL_FLOAT(app.visualizer.music.time, app.visualizer.director.time);
            TEST_ASSERT_EQUAL_UINT(app.visualizer.music.frame, app.visualizer.director.frame);
            TEST_ASSERT_EQUAL_INT(kind, app.visualizer.director.current.kind);
        }
    }
}

static void fast_network_reconnect(void)
{
    app_init(&app, 456, 0, &config);

    source.kind               = AUDIO_SOURCE_NETWORK;
    source.network_generation = 1;

    input(0, 0, 1);
    press(INPUT_R2);

    app.visualizer.music.time          = 12.0f;
    app.visualizer.music.frame         = 123;
    app.visualizer.music.attenuated[0] = 9.0f;

    Director director = app.visualizer.director;

    app_update(&app, &audio, &source, (InputState){ .connected = 1 }, now);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_VISUALIZER, app_screen(&app, &source));
    TEST_ASSERT_EQUAL_FLOAT(9.0f, app.visualizer.music.attenuated[0]);

    /* Disconnect and reconnect both occurred between application frames. */
    source.network_generation += 2;

    AppActions actions = app_update(&app, &audio, &source, (InputState){ .connected = 1 }, now);

    TEST_ASSERT_EQUAL_INT(UI_SCREEN_VISUALIZER, app_screen(&app, &source));

    MusicFeatures expected;

    music_init(&expected);

    expected.time  = 12.0f;
    expected.frame = 123;

    TEST_ASSERT_EQUAL_MEMORY(&expected, &app.visualizer.music, sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY(&director, &app.visualizer.director, sizeof(director));
    TEST_ASSERT_FALSE(actions.reset_scene);

    /* The same session preserves a subsequent display change too. */
    press(INPUT_R2);
    input(0, 0, 1);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_NETWORK_PLAYER, app_screen(&app, &source));
}

static void listening_fullscreen_delay(void)
{
    source.kind               = AUDIO_SOURCE_NETWORK;
    source.listening          = 1;
    source.network_generation = 1;

    /* Exercise the real five-second boundary across clock wrap, without a pad. */
    now = UINT32_MAX - 2000u;

    app_init(&app, 123, now, &config);
    input(0, 0, 0);

    now += 4979;

    input(0, 0, 0);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_NETWORK_PLAYER, app_screen(&app, &source));

    now -= 19;

    AppActions actions = input(0, 0, 0);

    TEST_ASSERT_EQUAL_INT(UI_SCREEN_VISUALIZER, app_screen(&app, &source));
    TEST_ASSERT_FALSE(actions.reset_scene);
    press(INPUT_R2);

    now += 6000;

    press(0);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_NETWORK_PLAYER, app_screen(&app, &source));

    /* A new session re-arms the introduction, even without a waiting frame. */
    ++source.network_generation;
    press(0);

    now += 4980;

    press(0);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_VISUALIZER, app_screen(&app, &source));

    source.network_waiting = 1;

    press(0);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_WAITING, app_screen(&app, &source));

    source.network_waiting = 0;
    source.listening       = 0;

    press(0);

    now += 6000;

    press(0);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_VISUALIZER, app_screen(&app, &source));
}

static void listening_fullscreen_respects_input(void)
{
    source.network_generation = 1;
    source.kind               = AUDIO_SOURCE_NETWORK;
    source.listening          = 1;

    press(0);
    press(INPUT_SELECT);

    now += 6000;

    press(0);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_NETWORK_PLAYER, app_screen(&app, &source));
    press(INPUT_SELECT);
    press(0);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_VISUALIZER, app_screen(&app, &source));

    ++source.network_generation;
    press(0);
    press(INPUT_R2);

    now += 6000;

    press(0);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_NETWORK_PLAYER, app_screen(&app, &source));
}

static void display_menu_and_shortcut_share_state(void)
{
    source.kind               = AUDIO_SOURCE_NETWORK;
    source.listening          = 1;
    source.network_generation = 1;

    press(0);
    press(INPUT_SELECT);
    press(INPUT_CROSS);

    app.settings.row = SETTING_DISPLAY;

    char label[64];

    settings_format(&app.settings, &app.visualizer.director, SETTING_DISPLAY, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("DISPLAY: PANELS", label);
    press(INPUT_CROSS);
    TEST_ASSERT_TRUE(app.settings.open);
    TEST_ASSERT_FALSE(app.listening_intro);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_VISUALIZER, app_screen(&app, &source));
    settings_format(&app.settings, &app.visualizer.director, SETTING_DISPLAY, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("DISPLAY: FULL SCREEN", label);
    press(INPUT_LEFT);
    press(INPUT_SELECT);

    now += 6000;

    press(0);
    TEST_ASSERT_EQUAL_INT(UI_SCREEN_NETWORK_PLAYER, app_screen(&app, &source));
    press(INPUT_R2);
    press(INPUT_SELECT);
    press(INPUT_CROSS);
    settings_format(&app.settings, &app.visualizer.director, SETTING_DISPLAY, label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("DISPLAY: FULL SCREEN", label);
}

static void exit_closes_menu_in_both_views(void)
{
    for (int panels = 0; panels < 2; ++panels)
    {
        app.settings.show_panels = panels;

        press(INPUT_SELECT);
        press(INPUT_UP);
        TEST_ASSERT_EQUAL_INT(MENU_EXIT_ROW, app.settings.menu_row);

        AppActions actions = press(INPUT_CROSS);

        TEST_ASSERT_FALSE(app.settings.open);
        TEST_ASSERT_EQUAL_INT(panels, app.settings.show_panels);
        TEST_ASSERT_EQUAL_UINT(0, actions.transport.command_count);
        TEST_ASSERT_FALSE(actions.reset_scene);
        press(INPUT_SELECT);
        TEST_ASSERT_TRUE(app.settings.open);
        press(INPUT_SELECT);
    }
}

static void sound_shortcut_and_menu_share_state(void)
{
    TEST_ASSERT_FALSE(app.settings.muted);

    for (int network = 0; network < 2; ++network)
    {
        source.kind = network ? AUDIO_SOURCE_NETWORK : AUDIO_SOURCE_CD;

        press(0);

        for (int panels = 0; panels < 2; ++panels)
        {
            app.settings.show_panels = panels;

            AppActions actions = press(INPUT_L2);

            TEST_ASSERT_TRUE(app.settings.muted);
            TEST_ASSERT_EQUAL_UINT(0, actions.transport.command_count);
            TEST_ASSERT_FALSE(actions.reset_scene);
            press(INPUT_SELECT);
            press(INPUT_DOWN);
            press(INPUT_CROSS);
            TEST_ASSERT_EQUAL_INT(MENU_PLAYBACK, app.settings.page);

            app.settings.menu_row = network ? 0 : PLAYBACK_SOUND_ROW;

            press(INPUT_CROSS);
            TEST_ASSERT_FALSE(app.settings.muted);
            press(INPUT_L2);
            TEST_ASSERT_TRUE(app.settings.muted);
            TEST_ASSERT_TRUE(app.settings.open);
            press(INPUT_LEFT);
            TEST_ASSERT_FALSE(app.settings.muted);
            press(INPUT_SELECT);
        }
    }

    app.player.editing = 1;

    press(INPUT_L2);
    TEST_ASSERT_TRUE(app.settings.muted);
}

static void listening_has_no_playback_menu_or_mute_shortcut(void)
{
    source.kind      = AUDIO_SOURCE_NETWORK;
    source.listening = 1;

    press(0);
    press(INPUT_L2);
    TEST_ASSERT_FALSE(app.settings.muted);
    press(INPUT_SELECT);
    press(INPUT_DOWN);
    TEST_ASSERT_EQUAL_INT(MENU_EXIT_ROW, app.settings.menu_row);
    press(INPUT_UP);
    TEST_ASSERT_EQUAL_INT(MENU_VISUALIZER_ROW, app.settings.menu_row);
    press(INPUT_UP);
    TEST_ASSERT_EQUAL_INT(MENU_EXIT_ROW, app.settings.menu_row);

    app.settings.page = MENU_PLAYBACK;

    press(0);
    TEST_ASSERT_EQUAL_INT(MENU_ROOT, app.settings.page);
}

static void configured_preferences(void)
{
    config.show_panels = config.levels = 0;
    config.muted                       = 1;

    app_init(&app, 123, now, &config);
    app_update(&app, &audio, &source, (InputState){ .connected = 1 }, ++now);
    TEST_ASSERT_FALSE(app.settings.show_panels);
    TEST_ASSERT_FALSE(app.settings.show_levels);
    TEST_ASSERT_TRUE(app.settings.muted);
    source.cd.generation++;
    app_update(&app, &audio, &source, (InputState){ .connected = 1 }, ++now);
    TEST_ASSERT_FALSE(app.settings.show_panels);
}

static void source_transitions_preserve_display_setting(void)
{
    for (int panels = 0; panels <= 1; ++panels)
    {
        config.show_panels = !panels;

        app_init(&app, 123, now, &config);

        app.settings.show_panels = panels;
        source.kind              = AUDIO_SOURCE_CD;

        press(0);
        TEST_ASSERT_EQUAL_INT(panels, app.settings.show_panels);
        TEST_ASSERT_EQUAL_INT(panels ? UI_SCREEN_CD_PLAYER : UI_SCREEN_VISUALIZER, app_screen(&app, &source));

        ++source.cd.generation;
        press(0);
        TEST_ASSERT_EQUAL_INT(panels, app.settings.show_panels);

        source.kind               = AUDIO_SOURCE_NETWORK;
        source.network_generation = 1;

        press(0);
        TEST_ASSERT_EQUAL_INT(panels, app.settings.show_panels);
        TEST_ASSERT_EQUAL_INT(panels ? UI_SCREEN_NETWORK_PLAYER : UI_SCREEN_VISUALIZER, app_screen(&app, &source));

        ++source.network_generation;
        press(0);
        TEST_ASSERT_EQUAL_INT(panels, app.settings.show_panels);

        source.network_waiting = 1;

        press(0);

        source.network_waiting = 0;

        press(0);
        TEST_ASSERT_EQUAL_INT(panels, app.settings.show_panels);
    }
}

static void cleanup_states_use_waiting_screen(void)
{
    source.network_waiting = 0;

    for (int failed = 0; failed <= 1; ++failed)
    {
        source.kind = failed ? AUDIO_SOURCE_ERROR : AUDIO_SOURCE_WAITING;

        press(0);

        for (int visible = 0; visible <= 1; ++visible)
        {
            app.settings.show_panels = visible;

            TEST_ASSERT_EQUAL_INT(UI_SCREEN_WAITING, app_screen(&app, &source));

            AppActions actions = press(INPUT_START | INPUT_R2);

            TEST_ASSERT_EQUAL_UINT(0, actions.transport.command_count);
            TEST_ASSERT_EQUAL_INT(visible, app.settings.show_panels);
            TEST_ASSERT_EQUAL_INT(UI_SCREEN_WAITING, app_screen(&app, &source));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(configured_preferences);
    RUN_TEST(source_transitions_preserve_display_setting);
    RUN_TEST(cleanup_states_use_waiting_screen);
    RUN_TEST(listening_has_no_playback_menu_or_mute_shortcut);
    RUN_TEST(sound_shortcut_and_menu_share_state);
    RUN_TEST(exit_closes_menu_in_both_views);
    RUN_TEST(display_menu_and_shortcut_share_state);
    RUN_TEST(fast_network_reconnect);
    RUN_TEST(cross_does_not_interrupt_visualization);
    RUN_TEST(listening_fullscreen_delay);
    RUN_TEST(listening_fullscreen_respects_input);
    RUN_TEST(global_transport_and_menu);
    RUN_TEST(program_returns_to_menu_and_preserves_saved_tracks);
    RUN_TEST(menus_and_network_ownership);
    RUN_TEST(disc_change_resets_program);
    RUN_TEST(listening_intro_and_disconnected_input);

    return UNITY_END();
}
