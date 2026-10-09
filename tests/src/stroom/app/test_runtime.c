#include "app/runtime.h"
#include "audio/network/cd_stream/stream.h"
#include "app/artwork.h"
#include "recognition/client.h"
#include "platform/platform.h"
#include "platform/memorycard/card.h"
#include "platform/input/pad.h"
#include "platform/time/clock.h"
#include "ui/frame/frame.h"
#include "ui/visualizer/scene.h"
#include "unity.h"
#include <string.h>

static int      stream_enabled, stream_close_result, updated_muted, expected_muted;
static int      stream_open_result, stream_prepared;
static uint32_t clock_ms, startup_screen_at, audio_opened_at, network_startup_delay;
static unsigned startup_frames, audio_polls;
static int      startup_transport_attempted;

/**
 * @brief Substitute cd_stream_open in this host fixture.
 */
int cd_stream_open(void)
{
    stream_prepared = stream_open_result == 0;

    return stream_open_result;
}

/**
 * @brief Substitute cd_stream_update in this host fixture.
 */
CdStreamStatus cd_stream_update(int enabled)
{
    stream_enabled = enabled;

    if (enabled && !startup_transport_attempted)
    {
        if (network_startup_delay)
        {
            TEST_ASSERT_GREATER_THAN_UINT(0, startup_frames);
            TEST_ASSERT_EQUAL_UINT(0, audio_polls);
        }

        startup_transport_attempted = 1;
        clock_ms += network_startup_delay;
    }

    return (CdStreamStatus){ .clients = enabled ? 2 : 0, .failed = stream_open_result != 0 };
}

/**
 * @brief Substitute cd_stream_close in this host fixture.
 */
int cd_stream_close(void)
{
    return stream_close_result;
}

enum
{
    PLATFORM_OPEN,
    PAD_OPEN,
    MEMORYCARD_OPEN,
    CONFIG_LOAD,
    NETWORK_CONFIGURE,
    LOOKUP_OPEN,
    AUDIO_OPEN,
    FRAME_OPEN,
    APP_INIT,
    ARTWORK_OPEN,
    AUDIO_POLL,
    LOOKUP_POLL,
    PAD_READ,
    APP_UPDATE,
    SCREEN,
    MUTE,
    TRANSPORT,
    SCENE_RESET,
    ARTWORK_PREPARE,
    RENDER,
    ARTWORK_CLOSE,
    LOOKUP_CLOSE,
    AUDIO_CLOSE,
    FRAME_CLOSE,
    PAD_CLOSE,
    PLATFORM_CLOSE
};

static AppRuntime      runtime;
static AppConfig       startup_config;
static int             events[128];
static unsigned        event_count;
static int             platform_ok, pad_ok, frame_ok, artwork_ok, audio_open_result;
static int             artwork_close_ok, lookup_close_ok, audio_close_ok, frame_close_ok, pad_close_ok, platform_close_ok;
static InputState      mock_input;
static AppActions      actions;
static UiScreen        selected_screen;
static AudioSourceKind polled_kind;
static unsigned        frames;
static int             expected_pad;
static const char*     expected_executable;
static int             memorycard_result;

/**
 * @brief Append a lifecycle event to the fixture trace.
 */
static void record(int event)
{
    TEST_ASSERT_LESS_THAN_UINT(128, event_count);

    events[event_count++] = event;
}

/**
 * @brief Assert the fixture lifecycle trace matches the expected event order.
 */
static void expect_events(const int* expected, unsigned count)
{
    TEST_ASSERT_EQUAL_UINT(count, event_count);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, events, count);

    event_count = 0;
}

/**
 * @brief Substitute platform_open in this host fixture.
 */
int platform_open(void)
{
    record(PLATFORM_OPEN);

    return platform_ok ? 0 : -1;
}

/**
 * @brief Substitute platform_close in this host fixture.
 */
int platform_close(void)
{
    record(PLATFORM_CLOSE);

    return platform_close_ok ? 0 : -1;
}

/**
 * @brief Substitute platform_pad_open in this host fixture.
 */
int platform_pad_open(void)
{
    record(PAD_OPEN);

    return pad_ok ? 0 : -1;
}

/**
 * @brief Substitute platform_pad_close in this host fixture.
 */
int platform_pad_close(int enabled)
{
    TEST_ASSERT_EQUAL_INT(1, enabled);
    record(PAD_CLOSE);

    return pad_close_ok ? 0 : -1;
}

/**
 * @brief Substitute platform_pad_read in this host fixture.
 */
InputState platform_pad_read(int enabled)
{
    TEST_ASSERT_EQUAL_INT(expected_pad, enabled);
    record(PAD_READ);

    return mock_input;
}

/**
 * @brief Substitute platform_memorycard_open in this host fixture.
 */
int platform_memorycard_open(const char* executable)
{
    TEST_ASSERT_EQUAL_STRING(expected_executable, executable);
    record(MEMORYCARD_OPEN);

    return memorycard_result;
}

/**
 * @brief Substitute app_config_load in this host fixture.
 */
AppConfig app_config_load(const char* executable)
{
    TEST_ASSERT_EQUAL_STRING(expected_executable, executable);
    record(CONFIG_LOAD);

    return startup_config;
}

/**
 * @brief Substitute app_config_defaults in this host fixture.
 */
AppConfig app_config_defaults(void)
{
    return (AppConfig){ .cd_autoplay = 1, .frame_rate = 30 };
}

/**
 * @brief Substitute platform_network_configure in this host fixture.
 */
void platform_network_configure(const NetworkConfig* config)
{
    TEST_ASSERT_EQUAL_MEMORY(&startup_config.network, config, sizeof(*config));
    record(NETWORK_CONFIGURE);
}

/**
 * @brief Substitute cd_lookup_open in this host fixture.
 */
void cd_lookup_open(const char* host)
{
    TEST_ASSERT_EQUAL_STRING(startup_config.lookup_host, host);
    record(LOOKUP_OPEN);
}

/**
 * @brief Substitute cd_lookup_close in this host fixture.
 */
int cd_lookup_close(void)
{
    record(LOOKUP_CLOSE);

    return lookup_close_ok ? 0 : -1;
}

/**
 * @brief Substitute audio_source_open in this host fixture.
 */
int audio_source_open(int autoplay, int muted)
{
    audio_opened_at = clock_ms;

    TEST_ASSERT_GREATER_THAN_UINT(0, startup_frames);

    if (stream_open_result == 0)
    {
        TEST_ASSERT_TRUE_MESSAGE(stream_prepared, "CD capture must be armed before starting autoplay");
    }

    TEST_ASSERT_EQUAL_INT(startup_config.cd_autoplay, autoplay);
    TEST_ASSERT_EQUAL_INT(startup_config.muted, muted);
    record(AUDIO_OPEN);

    return audio_open_result;
}

/**
 * @brief Substitute audio_source_close in this host fixture.
 */
int audio_source_close(void)
{
    record(AUDIO_CLOSE);

    return audio_close_ok ? 0 : -1;
}

/**
 * @brief Substitute ui_frame_open in this host fixture.
 */
int ui_frame_open(void)
{
    record(FRAME_OPEN);

    return frame_ok ? 0 : -1;
}

/**
 * @brief Substitute ui_frame_close in this host fixture.
 */
int ui_frame_close(void)
{
    record(FRAME_CLOSE);

    return frame_close_ok ? 0 : -1;
}

/**
 * @brief Substitute app_artwork_open in this host fixture.
 */
int app_artwork_open(void)
{
    record(ARTWORK_OPEN);

    return artwork_ok ? 0 : -1;
}

/**
 * @brief Substitute app_artwork_close in this host fixture.
 */
int app_artwork_close(void)
{
    record(ARTWORK_CLOSE);

    return artwork_close_ok ? 0 : -1;
}

/**
 * @brief Substitute platform_millis in this host fixture.
 */
uint32_t platform_millis(void)
{
    return clock_ms;
}

/**
 * @brief Substitute platform_ticks in this host fixture.
 */
uint64_t platform_ticks(void)
{
    return 123456;
}

/**
 * @brief Substitute app_init in this host fixture.
 */
void app_init(AppState* app, uint32_t seed, uint32_t now, const AppConfig* config)
{
    (void)seed;
    TEST_ASSERT_EQUAL_UINT32(clock_ms, now);
    TEST_ASSERT_EQUAL_MEMORY(&startup_config, config, sizeof(startup_config));

    app->settings.muted      = config->muted;
    app->settings.frame_rate = config->frame_rate;

    record(APP_INIT);
}

/**
 * @brief Substitute audio_source_poll in this host fixture.
 */
void audio_source_poll(Audio* audio, AudioSourceStatus* status)
{
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3000, clock_ms - startup_screen_at);
    ++audio_polls;
    memset(audio, 0, sizeof(*audio));
    memset(status, 0, sizeof(*status));

    if (audio_open_result != 0)
    {
        status->kind = AUDIO_SOURCE_ERROR;

        strcpy(status->error, "SOUND OUTPUT UNAVAILABLE - RESTART REQUIRED");
        record(AUDIO_POLL);
        return;
    }

    audio->active         = polled_kind == AUDIO_SOURCE_CD;
    status->kind          = polled_kind;
    status->cd.generation = polled_kind == AUDIO_SOURCE_CD ? 7 : 0;

    record(AUDIO_POLL);
}

/**
 * @brief Substitute cd_lookup_poll in this host fixture.
 */
void cd_lookup_poll(AudioSourceStatus* source)
{
    if (audio_open_result != 0)
    {
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, source->kind);
        record(LOOKUP_POLL);
        return;
    }

    TEST_ASSERT_EQUAL_INT(polled_kind, source->kind);

    if (polled_kind == AUDIO_SOURCE_CD)
    {
        strcpy(source->metadata.title, "Recognized track");
    }

    record(LOOKUP_POLL);
}

/**
 * @brief Substitute app_update in this host fixture.
 */
AppActions app_update(AppState* app, const Audio* audio, const AudioSourceStatus* source, InputState input, uint32_t now)
{
    TEST_ASSERT_EQUAL_INT(audio_open_result == 0 && polled_kind == AUDIO_SOURCE_CD, audio->active);
    TEST_ASSERT_EQUAL_STRING(audio_open_result == 0 && polled_kind == AUDIO_SOURCE_CD ? "Recognized track" : "", source->metadata.title);
    TEST_ASSERT_EQUAL_MEMORY(&mock_input, &input, sizeof(mock_input));
    TEST_ASSERT_EQUAL_UINT32(clock_ms, now);

    app->settings.muted      = updated_muted;
    app->settings.frame_rate = 30;

    record(APP_UPDATE);

    return actions;
}

/**
 * @brief Substitute app_screen in this host fixture.
 */
UiScreen app_screen(const AppState* app, const AudioSourceStatus* source)
{
    TEST_ASSERT_EQUAL_INT(updated_muted, app->settings.muted);
    TEST_ASSERT_EQUAL_UINT(audio_open_result == 0 && polled_kind == AUDIO_SOURCE_CD ? 7 : 0, source->cd.generation);
    record(SCREEN);

    return selected_screen;
}

/**
 * @brief Substitute audio_source_set_muted in this host fixture.
 */
void audio_source_set_muted(int muted)
{
    TEST_ASSERT_EQUAL_INT(expected_muted, muted);
    record(MUTE);
}

/**
 * @brief Substitute audio_source_apply in this host fixture.
 */
void audio_source_apply(const AudioSourceStatus* status, const AudioTransportRequests* requests)
{
    TEST_ASSERT_EQUAL_UINT(audio_open_result == 0 && polled_kind == AUDIO_SOURCE_CD ? 7 : 0, status->cd.generation);
    TEST_ASSERT_EQUAL_MEMORY(&actions.transport, requests, sizeof(*requests));
    record(TRANSPORT);
}

/**
 * @brief Substitute scene_reset in this host fixture.
 */
void scene_reset(void)
{
    record(SCENE_RESET);
}

/**
 * @brief Substitute app_artwork_prepare in this host fixture.
 */
void app_artwork_prepare(const AudioSourceStatus* source)
{
    TEST_ASSERT_EQUAL_STRING(audio_open_result == 0 && polled_kind == AUDIO_SOURCE_CD ? "Recognized track" : "", source->metadata.title);
    record(ARTWORK_PREPARE);
}

/**
 * @brief Substitute ui_frame_render in this host fixture.
 */
void ui_frame_render(UiScreen screen, const PlayerState* player, const AppSettings* settings, MilkdropRuntime* visualizer, const Audio* audio, const AudioSourceStatus* source, uint32_t now_ms)
{
    if (screen == UI_SCREEN_INITIALIZING)
    {
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_WAITING, source->kind);
        TEST_ASSERT_EQUAL_STRING("", source->error);
        TEST_ASSERT_FALSE(audio->active);
        TEST_ASSERT_EQUAL_UINT(0, audio_polls);

        if (!startup_frames)
        {
            startup_screen_at = clock_ms;
        }

        ++startup_frames;

        clock_ms += 1000; /* Simulated presentation; no real-time sleeps in tests. */

        return;
    }

    TEST_ASSERT_EQUAL_INT(selected_screen, screen);
    TEST_ASSERT_EQUAL_PTR(&runtime.app.player, player);
    TEST_ASSERT_EQUAL_PTR(&runtime.app.settings, settings);
    TEST_ASSERT_EQUAL_PTR(&runtime.app.visualizer, visualizer);
    TEST_ASSERT_EQUAL_PTR(&runtime.audio, audio);
    TEST_ASSERT_EQUAL_PTR(&runtime.source, source);
    TEST_ASSERT_EQUAL_INT(updated_muted, settings->muted);
    TEST_ASSERT_EQUAL_INT(30, settings->frame_rate);
    TEST_ASSERT_EQUAL_UINT32(clock_ms, now_ms);

    if (audio_open_result != 0)
    {
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, source->kind);
        TEST_ASSERT_EQUAL_STRING("SOUND OUTPUT UNAVAILABLE - RESTART REQUIRED", source->error);
        TEST_ASSERT_FALSE(audio->active);
    }

    ++frames;
    record(RENDER);
}

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    startup_screen_at           = 42;
    audio_opened_at             = 0;
    network_startup_delay       = 0;
    startup_frames              = 0;
    audio_polls                 = 0;
    startup_transport_attempted = 0;
    memorycard_result           = 0;
    stream_open_result          = 0;
    stream_prepared             = 0;

    memset(&runtime, 0, sizeof(runtime));
    memset(&startup_config, 0, sizeof(startup_config));
    memset(&actions, 0, sizeof(actions));

    stream_close_result        = 0;
    stream_enabled             = 0;
    expected_muted             = 1;
    updated_muted              = 1;
    audio_open_result          = 0;
    artwork_ok                 = 1;
    frame_ok                   = 1;
    pad_ok                     = 1;
    platform_ok                = 1;
    platform_close_ok          = 1;
    pad_close_ok               = 1;
    frame_close_ok             = 1;
    audio_close_ok             = 1;
    lookup_close_ok            = 1;
    artwork_close_ok           = 1;
    frames                     = 0;
    event_count                = 0;
    startup_config.cd_autoplay = 1;
    startup_config.frame_rate  = 60;
    startup_config.network.ip  = 0xc0a80131;

    strcpy(startup_config.lookup_host, "192.168.1.174");

    clock_ms            = 42;
    mock_input          = (InputState){ .pressed = INPUT_START, .connected = 1 };
    selected_screen     = UI_SCREEN_CD_PLAYER;
    polled_kind         = AUDIO_SOURCE_CD;
    expected_pad        = 1;
    expected_executable = "mc0:/BOOT/stroom-packed.elf";
}

/**
 * @brief Finish fixture cleanup and resource checks after each test.
 */
void tearDown(void)
{
    stream_close_result = 0;
    platform_close_ok   = 1;
    pad_close_ok        = 1;
    frame_close_ok      = 1;
    audio_close_ok      = 1;
    lookup_close_ok     = 1;
    artwork_close_ok    = 1;

    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);
}

/**
 * @brief Initialize the application runtime with fixture services.
 */
static int open_runtime(void)
{
    char* argv[] = { (char*)expected_executable };

    return app_runtime_open(&runtime, 1, argv);
}

/**
 * @brief Verify disc detection starts during initializing.
 */
static void disc_detection_starts_during_initializing(void)
{
    TEST_ASSERT_EQUAL_INT(0, open_runtime());
    TEST_ASSERT_LESS_THAN_UINT32(3000, audio_opened_at - startup_screen_at);
    TEST_ASSERT_EQUAL_UINT(0, audio_polls);
}

/**
 * @brief Verify startup holds all audio for three seconds.
 */
static void startup_holds_all_audio_for_three_seconds(void)
{
    TEST_ASSERT_EQUAL_INT(0, open_runtime());
    TEST_ASSERT_GREATER_THAN_UINT(0, startup_frames);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3000, clock_ms - startup_screen_at);
    TEST_ASSERT_TRUE(startup_transport_attempted);
    TEST_ASSERT_TRUE(stream_enabled);
    TEST_ASSERT_EQUAL_UINT(0, frames); /* No normal audio frames were polled during the hold. */
}

/**
 * @brief Verify slow network startup finishes before audio can start.
 */
static void slow_network_startup_finishes_before_audio_can_start(void)
{
    network_startup_delay = 4000;

    TEST_ASSERT_EQUAL_INT(0, open_runtime());
    TEST_ASSERT_GREATER_THAN_UINT(0, startup_frames);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4000, clock_ms - startup_screen_at);
    TEST_ASSERT_TRUE(startup_transport_attempted);
}

/**
 * @brief Verify unavailable network does not prevent audio after startup hold.
 */
static void unavailable_network_does_not_prevent_audio_after_startup_hold(void)
{
    stream_open_result = -1;

    TEST_ASSERT_EQUAL_INT(0, open_runtime());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3000, clock_ms - startup_screen_at);
    TEST_ASSERT_TRUE(runtime.ready);
    TEST_ASSERT_TRUE(runtime.audio_started);
}

/**
 * @brief Verify initialized transport stays ready during disc detection.
 */
static void initialized_transport_stays_ready_during_disc_detection(void)
{
    TEST_ASSERT_EQUAL_INT(0, open_runtime());

    polled_kind     = AUDIO_SOURCE_CHECKING;
    selected_screen = UI_SCREEN_DETECTING;

    app_runtime_step(&runtime);
    TEST_ASSERT_TRUE(stream_enabled);

    polled_kind     = AUDIO_SOURCE_CD;
    selected_screen = UI_SCREEN_CD_PLAYER;

    app_runtime_step(&runtime);
    TEST_ASSERT_TRUE(stream_enabled);
}

/**
 * @brief Verify startup and individual frames.
 */
static void startup_and_individual_frames(void)
{
    TEST_ASSERT_TRUE(open_runtime() == 0);

    const int startup[] = { PLATFORM_OPEN, PAD_OPEN, MEMORYCARD_OPEN, CONFIG_LOAD, NETWORK_CONFIGURE, FRAME_OPEN, APP_INIT, LOOKUP_OPEN, AUDIO_OPEN, ARTWORK_OPEN };

    expect_events(startup, sizeof(startup) / sizeof(*startup));
    TEST_ASSERT_TRUE(runtime.ready);
    TEST_ASSERT_TRUE(!(open_runtime() == 0));
    TEST_ASSERT_EQUAL_UINT(0, event_count);

    actions.reset_scene             = 1;
    actions.transport.commands[0]   = AUDIO_TRANSPORT_STOP;
    actions.transport.command_count = 1;

    app_runtime_step(&runtime);

    const int first[] = { AUDIO_POLL, LOOKUP_POLL, PAD_READ, APP_UPDATE, SCREEN, MUTE, TRANSPORT, SCENE_RESET, ARTWORK_PREPARE, RENDER };

    expect_events(first, sizeof(first) / sizeof(*first));
    TEST_ASSERT_EQUAL_UINT(1, frames);

    actions.reset_scene = 0;

    ++clock_ms;
    app_runtime_step(&runtime);

    const int second[] = { AUDIO_POLL, LOOKUP_POLL, PAD_READ, APP_UPDATE, SCREEN, MUTE, TRANSPORT, ARTWORK_PREPARE, RENDER };

    expect_events(second, sizeof(second) / sizeof(*second));
    TEST_ASSERT_EQUAL_UINT(2, frames);
}

/**
 * @brief Verify missing launch arguments use dev1 path.
 */
static void missing_launch_arguments_use_dev1_path(void)
{
    expected_executable = "mc0:/BOOT/BOOT.ELF";

    TEST_ASSERT_EQUAL_INT(0, app_runtime_open(&runtime, 0, NULL));

    const int startup[] = { PLATFORM_OPEN, PAD_OPEN, MEMORYCARD_OPEN, CONFIG_LOAD, NETWORK_CONFIGURE, FRAME_OPEN, APP_INIT, LOOKUP_OPEN, AUDIO_OPEN, ARTWORK_OPEN };

    expect_events(startup, sizeof(startup) / sizeof(*startup));
}

/**
 * @brief Verify stream startup failure keeps local audio available.
 */
static void stream_startup_failure_keeps_local_audio_available(void)
{
    stream_open_result = -1;

    TEST_ASSERT_EQUAL_INT(0, open_runtime());
    TEST_ASSERT_TRUE(runtime.ready);
    TEST_ASSERT_TRUE(runtime.audio_started);

    event_count = 0;

    app_runtime_step(&runtime);
    TEST_ASSERT_EQUAL_UINT(1, frames);
}

/**
 * @brief Verify unavailable card uses defaults without file access.
 */
static void unavailable_card_uses_defaults_without_file_access(void)
{
    memorycard_result = -1;
    startup_config    = app_config_defaults();

    TEST_ASSERT_EQUAL_INT(0, open_runtime());

    const int startup[] = { PLATFORM_OPEN, PAD_OPEN, MEMORYCARD_OPEN, NETWORK_CONFIGURE, FRAME_OPEN, APP_INIT, LOOKUP_OPEN, AUDIO_OPEN, ARTWORK_OPEN };

    expect_events(startup, sizeof(startup) / sizeof(*startup));
    TEST_ASSERT_TRUE(runtime.ready);
}

/**
 * @brief Verify audio start failure keeps ui and frames running.
 */
static void audio_start_failure_keeps_ui_and_frames_running(void)
{
    audio_open_result = AUDIO_SOURCE_ERROR_OUTPUT_RESTART_REQUIRED;
    selected_screen   = UI_SCREEN_WAITING;

    TEST_ASSERT_EQUAL_INT(0, open_runtime());

    const int startup[] = { PLATFORM_OPEN, PAD_OPEN, MEMORYCARD_OPEN, CONFIG_LOAD, NETWORK_CONFIGURE, FRAME_OPEN, APP_INIT, LOOKUP_OPEN, AUDIO_OPEN, ARTWORK_OPEN };

    expect_events(startup, sizeof(startup) / sizeof(*startup));
    TEST_ASSERT_TRUE(runtime.ready);
    TEST_ASSERT_TRUE(runtime.audio_started);
    TEST_ASSERT_TRUE(runtime.renderer_started);

    for (unsigned i = 0; i < 3; ++i)
    {
        app_runtime_step(&runtime);

        const int frame[] = { AUDIO_POLL, LOOKUP_POLL, PAD_READ, APP_UPDATE, SCREEN, MUTE, TRANSPORT, ARTWORK_PREPARE, RENDER };

        expect_events(frame, sizeof(frame) / sizeof(*frame));
    }

    TEST_ASSERT_EQUAL_UINT(3, frames);
    TEST_ASSERT_EQUAL_INT(0, app_runtime_close(&runtime));

    const int cleanup[] = { ARTWORK_CLOSE, LOOKUP_CLOSE, AUDIO_CLOSE, FRAME_CLOSE, PAD_CLOSE, PLATFORM_CLOSE };

    expect_events(cleanup, sizeof(cleanup) / sizeof(*cleanup));
}

/**
 * @brief Verify platform failure has no device cleanup.
 */
static void platform_failure_has_no_device_cleanup(void)
{
    platform_ok = 0;

    TEST_ASSERT_TRUE(!(open_runtime() == 0));

    const int expected[] = { PLATFORM_OPEN };

    expect_events(expected, 1);
    app_runtime_step(&runtime);
    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);
    TEST_ASSERT_EQUAL_UINT(0, event_count);
}

/**
 * @brief Verify failed renderer start retains cleanup stages.
 */
static void failed_renderer_start_retains_cleanup_stages(void)
{
    frame_ok = 0;

    TEST_ASSERT_EQUAL_INT(APP_RUNTIME_ERROR_RENDERER_START, open_runtime());
    TEST_ASSERT_FALSE(runtime.audio_started);
    TEST_ASSERT_FALSE(runtime.cd_stream_started);
    TEST_ASSERT_FALSE(runtime.lookup_started);

    event_count = 0;

    app_runtime_step(&runtime);
    TEST_ASSERT_EQUAL_UINT(0, event_count);

    frame_close_ok = 0;

    TEST_ASSERT_EQUAL_INT(APP_RUNTIME_ERROR_RENDERER_CLOSE, app_runtime_close(&runtime));

    const int display[] = { FRAME_CLOSE };

    expect_events(display, 1);
    TEST_ASSERT_EQUAL_INT(APP_RUNTIME_ERROR_ALREADY_OPEN, open_runtime());
    TEST_ASSERT_EQUAL_UINT(0, event_count);

    frame_close_ok = 1;

    TEST_ASSERT_EQUAL_INT(0, app_runtime_close(&runtime));

    const int finish[] = { FRAME_CLOSE, PAD_CLOSE, PLATFORM_CLOSE };

    expect_events(finish, 3);
    TEST_ASSERT_EQUAL_INT(0, app_runtime_close(&runtime));
    TEST_ASSERT_EQUAL_UINT(0, event_count);

    frame_ok = 1;

    TEST_ASSERT_EQUAL_INT(0, open_runtime());
}

/**
 * @brief Verify audio cleanup failure retains workers until retry.
 */
static void audio_cleanup_failure_retains_workers_until_retry(void)
{
    TEST_ASSERT_EQUAL_INT(0, open_runtime());

    event_count    = 0;
    audio_close_ok = 0;

    TEST_ASSERT_EQUAL_INT(APP_RUNTIME_ERROR_AUDIO_CLOSE, app_runtime_close(&runtime));
    TEST_ASSERT_TRUE(runtime.audio_started);
    TEST_ASSERT_TRUE(runtime.renderer_started);

    const int workers[] = { ARTWORK_CLOSE, LOOKUP_CLOSE, AUDIO_CLOSE };

    expect_events(workers, 3);
    TEST_ASSERT_EQUAL_INT(APP_RUNTIME_ERROR_ALREADY_OPEN, open_runtime());

    audio_close_ok = 1;

    TEST_ASSERT_EQUAL_INT(0, app_runtime_close(&runtime));

    const int finish[] = { AUDIO_CLOSE, FRAME_CLOSE, PAD_CLOSE, PLATFORM_CLOSE };

    expect_events(finish, 4);
}

/**
 * @brief Verify artwork failure does not prevent frames.
 */
static void artwork_failure_does_not_prevent_frames(void)
{
    artwork_ok = 0;

    TEST_ASSERT_TRUE(open_runtime() == 0);

    event_count = 0;

    app_runtime_step(&runtime);
    TEST_ASSERT_EQUAL_UINT(1, frames);

    event_count = 0;

    artwork_close_ok = 0;

    TEST_ASSERT_TRUE(!(app_runtime_close(&runtime) == 0));

    const int workers[] = { ARTWORK_CLOSE };

    expect_events(workers, 1);
    app_runtime_step(&runtime);
    TEST_ASSERT_EQUAL_UINT(0, event_count);

    artwork_close_ok = 1;

    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);

    const int finish[] = { ARTWORK_CLOSE, LOOKUP_CLOSE, AUDIO_CLOSE, FRAME_CLOSE, PAD_CLOSE, PLATFORM_CLOSE };

    expect_events(finish, 6);
}

/**
 * @brief Verify pad cleanup failure retains ownership until retry.
 */
static void pad_cleanup_failure_retains_ownership_until_retry(void)
{
    TEST_ASSERT_TRUE(open_runtime() == 0);

    event_count  = 0;
    pad_close_ok = 0;

    TEST_ASSERT_TRUE(!(app_runtime_close(&runtime) == 0));

    const int all[] = { ARTWORK_CLOSE, LOOKUP_CLOSE, AUDIO_CLOSE, FRAME_CLOSE, PAD_CLOSE };

    expect_events(all, 5);
    TEST_ASSERT_TRUE(!(open_runtime() == 0));
    app_runtime_step(&runtime);
    TEST_ASSERT_EQUAL_UINT(0, event_count);
    TEST_ASSERT_TRUE(!(app_runtime_close(&runtime) == 0));

    const int retry[] = { PAD_CLOSE };

    expect_events(retry, 1);

    pad_close_ok = 1;

    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);

    const int finish[] = { PAD_CLOSE, PLATFORM_CLOSE };

    expect_events(finish, 2);
    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);
    TEST_ASSERT_EQUAL_UINT(0, event_count);
    TEST_ASSERT_TRUE(open_runtime() == 0);
}

/**
 * @brief Verify platform cleanup failure retries only platform.
 */
static void platform_cleanup_failure_retries_only_platform(void)
{
    TEST_ASSERT_TRUE(open_runtime() == 0);

    event_count       = 0;
    platform_close_ok = 0;

    TEST_ASSERT_TRUE(!(app_runtime_close(&runtime) == 0));

    const int all[] = { ARTWORK_CLOSE, LOOKUP_CLOSE, AUDIO_CLOSE, FRAME_CLOSE, PAD_CLOSE, PLATFORM_CLOSE };

    expect_events(all, 6);
    TEST_ASSERT_TRUE(!(open_runtime() == 0));
    app_runtime_step(&runtime);
    TEST_ASSERT_EQUAL_UINT(0, event_count);

    platform_close_ok = 1;

    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);

    const int last[] = { PLATFORM_CLOSE };

    expect_events(last, 1);
}

/**
 * @brief Verify unavailable pad keeps frames enabled.
 */
static void unavailable_pad_keeps_frames_enabled(void)
{
    pad_ok              = 0;
    expected_pad        = 0;
    expected_executable = "mc0:/BOOT/BOOT.ELF";

    TEST_ASSERT_TRUE(app_runtime_open(&runtime, 0, NULL) == 0);

    const int startup[] = { PLATFORM_OPEN, PAD_OPEN, MEMORYCARD_OPEN, CONFIG_LOAD, NETWORK_CONFIGURE, FRAME_OPEN, APP_INIT, LOOKUP_OPEN, AUDIO_OPEN, ARTWORK_OPEN };

    expect_events(startup, sizeof(startup) / sizeof(*startup));
    app_runtime_step(&runtime);
    TEST_ASSERT_EQUAL_UINT(1, frames);

    event_count = 0;

    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);

    const int cleanup[] = { ARTWORK_CLOSE, LOOKUP_CLOSE, AUDIO_CLOSE, FRAME_CLOSE, PLATFORM_CLOSE };

    expect_events(cleanup, 5);
}

/**
 * @brief Verify cd stream is automatic and retains pending cleanup.
 */
static void cd_stream_is_automatic_and_retains_pending_cleanup(void)
{
    TEST_ASSERT_EQUAL_INT(0, open_runtime());

    expected_muted = 0;
    updated_muted  = 0;

    app_runtime_step(&runtime);
    TEST_ASSERT_TRUE(stream_enabled);

    expected_muted = 0;

    app_runtime_step(&runtime);
    TEST_ASSERT_TRUE(stream_enabled);

    audio_open_result = -1;

    app_runtime_step(&runtime);
    TEST_ASSERT_FALSE(stream_enabled);

    stream_close_result = 1;

    TEST_ASSERT_EQUAL_INT(1, app_runtime_close(&runtime));
    TEST_ASSERT_TRUE(runtime.cd_stream_started);
    TEST_ASSERT_TRUE(runtime.renderer_started);

    stream_close_result = -1;

    TEST_ASSERT_EQUAL_INT(APP_RUNTIME_ERROR_CD_STREAM_CLOSE, app_runtime_close(&runtime));

    stream_close_result = 0;

    TEST_ASSERT_EQUAL_INT(0, app_runtime_close(&runtime));
    TEST_ASSERT_FALSE(runtime.cd_stream_started);
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(disc_detection_starts_during_initializing);
    RUN_TEST(startup_holds_all_audio_for_three_seconds);
    RUN_TEST(initialized_transport_stays_ready_during_disc_detection);
    RUN_TEST(slow_network_startup_finishes_before_audio_can_start);
    RUN_TEST(unavailable_network_does_not_prevent_audio_after_startup_hold);
    RUN_TEST(cd_stream_is_automatic_and_retains_pending_cleanup);
    RUN_TEST(startup_and_individual_frames);
    RUN_TEST(missing_launch_arguments_use_dev1_path);
    RUN_TEST(stream_startup_failure_keeps_local_audio_available);
    RUN_TEST(unavailable_card_uses_defaults_without_file_access);
    RUN_TEST(audio_start_failure_keeps_ui_and_frames_running);
    RUN_TEST(platform_failure_has_no_device_cleanup);
    RUN_TEST(failed_renderer_start_retains_cleanup_stages);
    RUN_TEST(audio_cleanup_failure_retains_workers_until_retry);
    RUN_TEST(artwork_failure_does_not_prevent_frames);
    RUN_TEST(pad_cleanup_failure_retains_ownership_until_retry);
    RUN_TEST(platform_cleanup_failure_retries_only_platform);
    RUN_TEST(unavailable_pad_keeps_frames_enabled);

    return UNITY_END();
}
