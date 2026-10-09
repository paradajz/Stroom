#include "app/controller.h"
#include "ui/visualizer/controller.h"
#include <string.h>

#define LISTENING_INTRO_MS 5000u

void app_init(AppState* app, uint32_t seed, uint32_t now, const AppConfig* config)
{
    memset(app, 0, sizeof(*app));

    app->settings.show_levels = config->levels;
    app->settings.frame_rate  = config->frame_rate;
    app->settings.muted       = config->muted;
    app->settings.show_panels = config->show_panels;

    player_init(&app->player);
    milkdrop_runtime_init(&app->visualizer, seed);
    director_set_frame_rate(&app->visualizer.director, config->frame_rate);

    app->previous_source     = AUDIO_SOURCE_NETWORK;
    app->was_network_waiting = 1;
    app->last_frame          = now;
}

UiScreen app_screen(const AppState* app, const AudioSourceStatus* source)
{
    if (source->kind == AUDIO_SOURCE_CHECKING)
    {
        return UI_SCREEN_DETECTING;
    }

    if (source->network_waiting || source->kind == AUDIO_SOURCE_WAITING || source->kind == AUDIO_SOURCE_ERROR)
    {
        return UI_SCREEN_WAITING;
    }

    if (app->player.editing && source->kind == AUDIO_SOURCE_CD)
    {
        return UI_SCREEN_CD_PLAYER;
    }

    return app->settings.show_panels ? (source->kind == AUDIO_SOURCE_CD ? UI_SCREEN_CD_PLAYER : UI_SCREEN_NETWORK_PLAYER) : UI_SCREEN_VISUALIZER;
}

AppActions app_update(AppState* app, const Audio* audio, const AudioSourceStatus* source, InputState input, uint32_t now)
{
    AppActions effects = { 0 };
    float      dt      = (uint32_t)(now - app->last_frame) / 1000.0f;

    app->last_frame = now;

    if (source->kind != app->previous_source || (source->kind == AUDIO_SOURCE_CD && source->cd.generation != app->previous_disc))
    {
        app->previous_source = source->kind;
        app->previous_disc   = source->cd.generation;

        player_init(&app->player);

        app->settings.open = 0;

        milkdrop_runtime_reset_audio(&app->visualizer);
    }

    if (source->kind == AUDIO_SOURCE_NETWORK && !source->network_waiting &&
        (app->was_network_waiting || source->network_generation != app->previous_network))
    {
        milkdrop_runtime_reset_audio(&app->visualizer);

        app->previous_network = source->network_generation;
        app->listening_intro  = source->listening;
        app->listening_since  = now;
    }

    if (source->kind != AUDIO_SOURCE_NETWORK || source->network_waiting || !source->listening)
    {
        app->listening_intro = 0;
    }

    app->was_network_waiting = source->network_waiting || source->kind == AUDIO_SOURCE_WAITING;

    if (!input.connected)
    {
        input.held = input.pressed = 0;
    }

    unsigned pressed = input.pressed;

    if ((pressed & INPUT_L2) && !source->listening)
    {
        app->settings.muted = !app->settings.muted;
    }

    pressed &= ~INPUT_L2;

    if (source->listening && app->settings.page == MENU_PLAYBACK)
    {
        app->settings.page     = MENU_ROOT;
        app->settings.menu_row = 0;
    }

    int cd_active   = source->kind == AUDIO_SOURCE_CD;
    int was_editing = app->player.editing;
    int menu_owned  = app->settings.open;

    /* Start is transport regardless of the currently visible screen/menu. */

    if ((pressed & INPUT_START) && cd_active)
    {
        effects.transport = player_update(&app->player, &source->cd, INPUT_START, 0, 1, now);
        pressed           = 0;
    }
    else
    {
        pressed &= ~INPUT_START;
    }

    if (pressed & INPUT_R2)
    {
        if (!was_editing && !source->network_waiting && source->kind != AUDIO_SOURCE_CHECKING && source->kind != AUDIO_SOURCE_WAITING && source->kind != AUDIO_SOURCE_ERROR)
        {
            app->settings.show_panels = !app->settings.show_panels;
            app->listening_intro      = 0;
            app->settings.open        = 0;
        }

        menu_owned = 1;
        pressed    = 0;
    }

    if ((pressed & INPUT_SELECT) && !was_editing)
    {
        if (app->settings.open)
        {
            app->settings.open = 0;
        }
        else
        {
            settings_open(&app->settings, &source->cd);
        }

        menu_owned = 1;
        pressed    = 0;
    }

    if (app->settings.open)
    {
        if (pressed & INPUT_TRIANGLE)
        {
            if (app->settings.page == MENU_ROOT)
            {
                app->settings.open = 0;
            }
            else
            {
                app->settings.page     = MENU_ROOT;
                app->settings.menu_row = 0;
            }
        }
        else if (app->settings.page == MENU_VISUALIZER)
        {
            int show_panels = app->settings.show_panels;
            int frame_rate  = app->settings.frame_rate;

            settings_update(&app->settings, &app->visualizer.director, pressed);

            effects.reset_scene |= frame_rate != app->settings.frame_rate;

            if (show_panels != app->settings.show_panels)
            {
                app->listening_intro = 0;
            }
        }
        else if (app->settings.page == MENU_PLAYBACK)
        {
            AudioTransportRequests menu = settings_playback_update(&app->settings, &app->player, cd_active ? &source->cd : NULL, pressed);

            if (menu.command_count)
            {
                effects.transport = menu;
            }
        }
        else if (app->settings.page == MENU_ROOT)
        {
            if (pressed & (INPUT_UP | INPUT_DOWN))
            {
                do
                {
                    app->settings.menu_row = (app->settings.menu_row + ((pressed & INPUT_UP) ? MENU_ROOT_COUNT - 1 : 1)) % MENU_ROOT_COUNT;
                } while (source->listening && app->settings.menu_row == MENU_PLAYBACK_ROW);
            }

            if (pressed & INPUT_CROSS)
            {
                if (app->settings.menu_row == MENU_VISUALIZER_ROW)
                {
                    app->settings.page = MENU_VISUALIZER;
                }
                else if (app->settings.menu_row == MENU_PLAYBACK_ROW && !source->listening)
                {
                    app->settings.page     = MENU_PLAYBACK;
                    app->settings.menu_row = 0;
                }
                else if (app->settings.menu_row == MENU_EXIT_ROW)
                {
                    app->settings.open = 0;
                }
            }
        }

        pressed = 0;
    }

    if (menu_owned)
    {
        pressed = 0;
    }

    /* Show the sender briefly on connection; the menu pauses the automatic transition. */

    if (app->listening_intro && !menu_owned && (uint32_t)(now - app->listening_since) >= LISTENING_INTRO_MS)
    {
        app->settings.show_panels = 0;
        app->listening_intro      = 0;
    }

    UiScreen               screen    = app_screen(app, source);
    AudioTransportRequests transport = player_update(&app->player, &source->cd, pressed, input.held, cd_active && !menu_owned && input.connected, now);

    if (transport.command_count || transport.program_count || !effects.transport.command_count)
    {
        effects.transport = transport;
    }

    if (was_editing && !app->player.editing)
    {
        app->settings.open     = 1;
        app->settings.page     = MENU_PLAYBACK;
        app->settings.menu_row = PLAYBACK_PROGRAM_ROW;

        if (effects.transport.program_count)
        {
            app->settings.playback_mode = PLAYBACK_PROGRAM;
        }
    }

    /* A frame belongs to a single input owner, even when it changes screens. */

    if (screen == UI_SCREEN_VISUALIZER && !menu_owned && !was_editing)
    {
        effects.reset_scene |= visualizer_input(&app->visualizer, cd_active ? pressed & ~(INPUT_TRIANGLE | INPUT_L1 | INPUT_R1) : pressed);
    }

    milkdrop_runtime_step(&app->visualizer, audio, app->visualizer_started ? dt : 0);

    app->visualizer_started = 1;

    return effects;
}
