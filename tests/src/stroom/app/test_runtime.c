#include "app/runtime.h"
#include "app/artwork.h"
#include "recognition/client.h"
#include "platform/platform.h"
#include "platform/input/pad.h"
#include "platform/time/clock.h"
#include "ui/frame/frame.h"
#include "ui/visualizer/scene.h"
#include "unity.h"
#include <string.h>

enum
{
    PLATFORM_OPEN,
    PAD_OPEN,
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

static AppRuntime  runtime;
static AppConfig   startup_config;
static int         events[128];
static unsigned    event_count;
static int         platform_ok, pad_ok, frame_ok, artwork_ok, audio_open_result;
static int         artwork_close_ok, lookup_close_ok, audio_close_ok, frame_close_ok, pad_close_ok, platform_close_ok;
static uint32_t    clock_ms;
static InputState  mock_input;
static AppActions  actions;
static UiScreen    selected_screen;
static unsigned    frames;
static int         expected_pad;
static const char* expected_executable;

static void record(int event)
{
    TEST_ASSERT_LESS_THAN_UINT(128, event_count);

    events[event_count++] = event;
}

static void expect_events(const int* expected, unsigned count)
{
    TEST_ASSERT_EQUAL_UINT(count, event_count);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, events, count);

    event_count = 0;
}

int platform_open(void)
{
    record(PLATFORM_OPEN);

    return platform_ok ? 0 : -1;
}

int platform_close(void)
{
    record(PLATFORM_CLOSE);

    return platform_close_ok ? 0 : -1;
}

int platform_pad_open(void)
{
    record(PAD_OPEN);

    return pad_ok ? 0 : -1;
}

int platform_pad_close(int enabled)
{
    TEST_ASSERT_EQUAL_INT(1, enabled);
    record(PAD_CLOSE);

    return pad_close_ok ? 0 : -1;
}

InputState platform_pad_read(int enabled)
{
    TEST_ASSERT_EQUAL_INT(expected_pad, enabled);
    record(PAD_READ);

    return mock_input;
}

AppConfig app_config_load(const char* executable)
{
    TEST_ASSERT_EQUAL_STRING(expected_executable, executable);
    record(CONFIG_LOAD);

    return startup_config;
}

void platform_network_configure(const NetworkConfig* config)
{
    TEST_ASSERT_EQUAL_MEMORY(&startup_config.network, config, sizeof(*config));
    record(NETWORK_CONFIGURE);
}

void cd_lookup_open(const char* host)
{
    TEST_ASSERT_EQUAL_STRING(startup_config.lookup_host, host);
    record(LOOKUP_OPEN);
}

int cd_lookup_close(void)
{
    record(LOOKUP_CLOSE);

    return lookup_close_ok ? 0 : -1;
}

int audio_source_open(int autoplay, int muted)
{
    TEST_ASSERT_EQUAL_INT(startup_config.cd_autoplay, autoplay);
    TEST_ASSERT_EQUAL_INT(startup_config.muted, muted);
    record(AUDIO_OPEN);

    return audio_open_result;
}

int audio_source_close(void)
{
    record(AUDIO_CLOSE);

    return audio_close_ok ? 0 : -1;
}

int ui_frame_open(void)
{
    record(FRAME_OPEN);

    return frame_ok ? 0 : -1;
}

int ui_frame_close(void)
{
    record(FRAME_CLOSE);

    return frame_close_ok ? 0 : -1;
}

int app_artwork_open(void)
{
    record(ARTWORK_OPEN);

    return artwork_ok ? 0 : -1;
}

int app_artwork_close(void)
{
    record(ARTWORK_CLOSE);

    return artwork_close_ok ? 0 : -1;
}

uint32_t platform_millis(void)
{
    return clock_ms;
}

uint64_t platform_ticks(void)
{
    return 123456;
}

void app_init(AppState* app, uint32_t seed, uint32_t now, const AppConfig* config)
{
    (void)seed;
    TEST_ASSERT_EQUAL_UINT32(clock_ms, now);
    TEST_ASSERT_EQUAL_MEMORY(&startup_config, config, sizeof(startup_config));

    app->settings.muted      = config->muted;
    app->settings.frame_rate = config->frame_rate;

    record(APP_INIT);
}

void audio_source_poll(Audio* audio, AudioSourceStatus* status)
{
    memset(audio, 0, sizeof(*audio));
    memset(status, 0, sizeof(*status));

    if (audio_open_result != 0)
    {
        status->kind = AUDIO_SOURCE_ERROR;

        strcpy(status->error, "SOUND OUTPUT UNAVAILABLE - RESTART REQUIRED");
        record(AUDIO_POLL);
        return;
    }

    audio->active         = 1;
    status->kind          = AUDIO_SOURCE_CD;
    status->cd.generation = 7;

    record(AUDIO_POLL);
}

void cd_lookup_poll(AudioSourceStatus* source)
{
    if (audio_open_result != 0)
    {
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, source->kind);
        record(LOOKUP_POLL);
        return;
    }

    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, source->kind);
    strcpy(source->metadata.title, "Recognized track");
    record(LOOKUP_POLL);
}

AppActions app_update(AppState* app, const Audio* audio, const AudioSourceStatus* source, InputState input, uint32_t now)
{
    TEST_ASSERT_EQUAL_INT(audio_open_result == 0, audio->active);
    TEST_ASSERT_EQUAL_STRING(audio_open_result == 0 ? "Recognized track" : "", source->metadata.title);
    TEST_ASSERT_EQUAL_MEMORY(&mock_input, &input, sizeof(mock_input));
    TEST_ASSERT_EQUAL_UINT32(clock_ms, now);

    app->settings.muted      = 1;
    app->settings.frame_rate = 30;

    record(APP_UPDATE);

    return actions;
}

UiScreen app_screen(const AppState* app, const AudioSourceStatus* source)
{
    TEST_ASSERT_TRUE(app->settings.muted);
    TEST_ASSERT_EQUAL_UINT(audio_open_result == 0 ? 7 : 0, source->cd.generation);
    record(SCREEN);

    return selected_screen;
}

void audio_source_set_muted(int muted)
{
    TEST_ASSERT_EQUAL_INT(1, muted);
    record(MUTE);
}

void audio_source_apply(const AudioSourceStatus* status, const AudioTransportRequests* requests)
{
    TEST_ASSERT_EQUAL_UINT(audio_open_result == 0 ? 7 : 0, status->cd.generation);
    TEST_ASSERT_EQUAL_MEMORY(&actions.transport, requests, sizeof(*requests));
    record(TRANSPORT);
}

void scene_reset(void)
{
    record(SCENE_RESET);
}

void app_artwork_prepare(const AudioSourceStatus* source)
{
    TEST_ASSERT_EQUAL_STRING(audio_open_result == 0 ? "Recognized track" : "", source->metadata.title);
    record(ARTWORK_PREPARE);
}

void ui_frame_render(UiScreen screen, const PlayerState* player, const AppSettings* settings, MilkdropRuntime* visualizer, const Audio* audio, const AudioSourceStatus* source, uint32_t now_ms)
{
    TEST_ASSERT_EQUAL_INT(selected_screen, screen);
    TEST_ASSERT_EQUAL_PTR(&runtime.app.player, player);
    TEST_ASSERT_EQUAL_PTR(&runtime.app.settings, settings);
    TEST_ASSERT_EQUAL_PTR(&runtime.app.visualizer, visualizer);
    TEST_ASSERT_EQUAL_PTR(&runtime.audio, audio);
    TEST_ASSERT_EQUAL_PTR(&runtime.source, source);
    TEST_ASSERT_EQUAL_INT(1, settings->muted);
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

void setUp(void)
{
    memset(&runtime, 0, sizeof(runtime));
    memset(&startup_config, 0, sizeof(startup_config));
    memset(&actions, 0, sizeof(actions));

    audio_open_result = 0;
    platform_ok = pad_ok = frame_ok = artwork_ok = 1;
    artwork_close_ok = lookup_close_ok = audio_close_ok = frame_close_ok = pad_close_ok = platform_close_ok = 1;
    event_count = frames       = 0;
    startup_config.cd_autoplay = 1;
    startup_config.frame_rate  = 60;
    startup_config.network.ip  = 0xc0a80131;

    strcpy(startup_config.lookup_host, "192.168.1.174");

    clock_ms            = 42;
    mock_input          = (InputState){ .pressed = INPUT_START, .connected = 1 };
    selected_screen     = UI_SCREEN_CD_PLAYER;
    expected_pad        = 1;
    expected_executable = "mc0:/BOOT/stroom-packed.elf";
}

void tearDown(void)
{
    artwork_close_ok = lookup_close_ok = audio_close_ok = frame_close_ok = pad_close_ok = platform_close_ok = 1;

    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);
}

static int open_runtime(void)
{
    char* argv[] = { (char*)expected_executable };

    return app_runtime_open(&runtime, 1, argv);
}

static void startup_and_individual_frames(void)
{
    TEST_ASSERT_TRUE(open_runtime() == 0);

    const int startup[] = { PLATFORM_OPEN, PAD_OPEN, CONFIG_LOAD, NETWORK_CONFIGURE, LOOKUP_OPEN, AUDIO_OPEN, FRAME_OPEN, APP_INIT, ARTWORK_OPEN };

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

static void audio_start_failure_keeps_ui_and_frames_running(void)
{
    audio_open_result = AUDIO_SOURCE_ERROR_OUTPUT_RESTART_REQUIRED;
    selected_screen   = UI_SCREEN_WAITING;

    TEST_ASSERT_EQUAL_INT(0, open_runtime());

    const int startup[] = { PLATFORM_OPEN, PAD_OPEN, CONFIG_LOAD, NETWORK_CONFIGURE, LOOKUP_OPEN, AUDIO_OPEN, FRAME_OPEN, APP_INIT, ARTWORK_OPEN };

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

static void failed_renderer_start_retains_cleanup_stages(void)
{
    frame_ok = 0;

    TEST_ASSERT_TRUE(!(open_runtime() == 0));

    event_count = 0;

    app_runtime_step(&runtime);
    TEST_ASSERT_EQUAL_UINT(0, event_count);

    lookup_close_ok = audio_close_ok = 0;

    TEST_ASSERT_TRUE(!(app_runtime_close(&runtime) == 0));

    const int workers[] = { LOOKUP_CLOSE, AUDIO_CLOSE };

    expect_events(workers, 2);
    TEST_ASSERT_TRUE(!(open_runtime() == 0));
    TEST_ASSERT_EQUAL_UINT(0, event_count);

    lookup_close_ok = 1;

    TEST_ASSERT_TRUE(!(app_runtime_close(&runtime) == 0));
    expect_events(workers, 2);

    audio_close_ok = 1;
    frame_close_ok = 0;

    TEST_ASSERT_TRUE(!(app_runtime_close(&runtime) == 0));

    const int display[] = { AUDIO_CLOSE, FRAME_CLOSE };

    expect_events(display, 2);
    TEST_ASSERT_TRUE(!(open_runtime() == 0));

    frame_close_ok = 1;

    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);

    const int finish[] = { FRAME_CLOSE, PAD_CLOSE, PLATFORM_CLOSE };

    expect_events(finish, 3);
    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);
    TEST_ASSERT_EQUAL_UINT(0, event_count);

    frame_ok = 1;

    TEST_ASSERT_TRUE(open_runtime() == 0);
}

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

static void unavailable_pad_keeps_frames_enabled(void)
{
    pad_ok              = 0;
    expected_pad        = 0;
    expected_executable = "";

    TEST_ASSERT_TRUE(app_runtime_open(&runtime, 0, NULL) == 0);

    const int startup[] = { PLATFORM_OPEN, PAD_OPEN, CONFIG_LOAD, NETWORK_CONFIGURE, LOOKUP_OPEN, AUDIO_OPEN, FRAME_OPEN, APP_INIT, ARTWORK_OPEN };

    expect_events(startup, 9);
    app_runtime_step(&runtime);
    TEST_ASSERT_EQUAL_UINT(1, frames);

    event_count = 0;

    TEST_ASSERT_TRUE(app_runtime_close(&runtime) == 0);

    const int cleanup[] = { ARTWORK_CLOSE, LOOKUP_CLOSE, AUDIO_CLOSE, FRAME_CLOSE, PLATFORM_CLOSE };

    expect_events(cleanup, 5);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(startup_and_individual_frames);
    RUN_TEST(audio_start_failure_keeps_ui_and_frames_running);
    RUN_TEST(platform_failure_has_no_device_cleanup);
    RUN_TEST(failed_renderer_start_retains_cleanup_stages);
    RUN_TEST(artwork_failure_does_not_prevent_frames);
    RUN_TEST(pad_cleanup_failure_retains_ownership_until_retry);
    RUN_TEST(platform_cleanup_failure_retries_only_platform);
    RUN_TEST(unavailable_pad_keeps_frames_enabled);

    return UNITY_END();
}
