#include "app/runtime.h"
#include "util/diagnostics.h"
#include "platform/platform.h"
#include "app/controller.h"
#include "app/artwork.h"
#include "recognition/client.h"
#include "audio/source/source.h"
#include "platform/time/clock.h"
#include "platform/input/pad.h"
#include "ui/frame/frame.h"
#include "ui/visualizer/scene.h"
#include <string.h>

#define STARTUP_SEED_SALT 0x4b53e2a1u

int app_runtime_open(AppRuntime* runtime, int argc, char** argv)
{
    if (runtime->platform_opened || runtime->pad_open || runtime->lookup_started ||
        runtime->audio_started || runtime->renderer_started || runtime->artwork_started ||
        runtime->ready || runtime->closing)
    {
        return APP_RUNTIME_ERROR_ALREADY_OPEN;
    }

    memset(runtime, 0, sizeof(*runtime));

    if (platform_open() != 0)
    {
        return APP_RUNTIME_ERROR_PLATFORM_START;
    }

    runtime->platform_opened = 1;

    STROOM_LOG("starting");

    runtime->pad_open = platform_pad_open() == 0;
    AppConfig config  = app_config_load(argc > 0 ? argv[0] : "");

    platform_network_configure(&config.network);

    runtime->lookup_started = 1;

    cd_lookup_open(config.lookup_host);

    runtime->audio_started = 1;

    int audio_result = audio_source_open(config.cd_autoplay, config.muted);

    if (audio_result != 0)
    {
        STROOM_LOG("audio startup unavailable: %d; continuing with UI", audio_result);
    }

    runtime->renderer_started = 1;

    if (ui_frame_open() != 0)
    {
        return APP_RUNTIME_ERROR_RENDERER_START;
    }

    app_init(&runtime->app, (uint32_t)platform_ticks() ^ STARTUP_SEED_SALT, platform_millis(), &config);
    STROOM_LOG("visualizer ready");

    runtime->artwork_started = 1;

    if (app_artwork_open() != 0)
    {
        STROOM_LOG("artwork decoder unavailable");
    }

    runtime->ready = 1;

    return 0;
}

void app_runtime_step(AppRuntime* runtime)
{
    if (!runtime->ready)
    {
        return;
    }

    uint32_t now = platform_millis();

    audio_source_poll(&runtime->audio, &runtime->source);
    cd_lookup_poll(&runtime->source);

    InputState input   = platform_pad_read(runtime->pad_open);
    AppActions actions = app_update(&runtime->app, &runtime->audio, &runtime->source, input, now);
    UiScreen   screen  = app_screen(&runtime->app, &runtime->source);

    audio_source_set_muted(runtime->app.settings.muted);
    audio_source_apply(&runtime->source, &actions.transport);

    if (actions.reset_scene)
    {
        scene_reset();
    }

    app_artwork_prepare(&runtime->source);
    ui_frame_render(screen, &runtime->app.player, &runtime->app.settings, &runtime->app.visualizer, &runtime->audio, &runtime->source, now);
}

int app_runtime_close(AppRuntime* runtime)
{
    runtime->ready   = 0;
    runtime->closing = 1;

    if (runtime->artwork_started)
    {
        /* Decoder diagnostics can still use the network worker until joining succeeds. */
        int result = app_artwork_close();

        if (result != 0)
        {
            return result < 0 ? APP_RUNTIME_ERROR_ARTWORK_CLOSE : result;
        }

        runtime->artwork_started = 0;
    }

    int lookup_result = runtime->lookup_started ? cd_lookup_close() : 0;
    int audio_result  = runtime->audio_started ? audio_source_close() : 0;

    if (lookup_result == 0)
    {
        runtime->lookup_started = 0;
    }

    if (audio_result == 0)
    {
        runtime->audio_started = 0;
    }

    if (lookup_result < 0 || audio_result < 0)
    {
        return lookup_result < 0 ? APP_RUNTIME_ERROR_LOOKUP_CLOSE : APP_RUNTIME_ERROR_AUDIO_CLOSE;
    }

    if (lookup_result > 0 || audio_result > 0)
    {
        return 1;
    }

    if (runtime->renderer_started)
    {
        int result = ui_frame_close();

        if (result != 0)
        {
            return result < 0 ? APP_RUNTIME_ERROR_RENDERER_CLOSE : result;
        }

        runtime->renderer_started = 0;
    }

    if (runtime->pad_open)
    {
        int result = platform_pad_close(runtime->pad_open);

        if (result != 0)
        {
            return result < 0 ? APP_RUNTIME_ERROR_PAD_CLOSE : result;
        }

        runtime->pad_open = 0;
    }

    if (runtime->platform_opened)
    {
        int result = platform_close();

        if (result != 0)
        {
            return result < 0 ? APP_RUNTIME_ERROR_PLATFORM_CLOSE : result;
        }

        runtime->platform_opened = 0;
    }

    runtime->closing = 0;

    return 0;
}
