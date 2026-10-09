#include "contracts/milkdrop.h"
#include "ui/settings/controller.h"
#include <stdio.h>

static const char* const playback_names[] = { "CONTINUE", "SHUFFLE", "REPEAT CURRENT", "REPEAT ALL", "PROGRAM" };

void settings_update(AppSettings* settings, Director* director, unsigned pressed)
{
    if (pressed & (INPUT_UP | INPUT_DOWN))
    {
        settings->row = (settings->row + ((pressed & INPUT_UP) ? SETTING_COUNT - 1 : 1)) % SETTING_COUNT;
    }

    if (!(pressed & (INPUT_LEFT | INPUT_RIGHT | INPUT_CROSS)))
    {
        return;
    }

    int direction = (pressed & INPUT_LEFT) ? -1 : 1;

    switch (settings->row)
    {
    case SETTING_DISPLAY:
        settings->show_panels = !settings->show_panels;

        break;
    case SETTING_FRAME_RATE:

        if (director_set_frame_rate(director, settings->frame_rate == MILKDROP_FPS_BASELINE ? MILKDROP_FPS_HIGH : MILKDROP_FPS_BASELINE) == 0)
        {
            settings->frame_rate = director->frame_rate;
        }

        break;
    case SETTING_MODE:
        director_set_mode(director, (director->mode + DIRECTOR_MODE_COUNT + direction) % DIRECTOR_MODE_COUNT);
        break;
    case SETTING_PRESET_NAME:
        settings->show_name = !settings->show_name;

        break;
    case SETTING_LEVEL_METER:
        settings->show_levels = !settings->show_levels;

        break;
    case SETTING_INTERVAL:
        director_adjust_duration(director, direction);
        break;
    case SETTING_HARD_CUTS:
        director->hard_cuts = !director->hard_cuts;

        break;
    case SETTING_VARIATION:
        director_adjust_variation(director, direction);
        break;
    default:
        break;
    }
}

void settings_format(const AppSettings* settings, const Director* director, SettingRow row, char* text, size_t size)
{
    switch (row)
    {
    case SETTING_DISPLAY:
        snprintf(text, size, "DISPLAY: %s", settings->show_panels ? "PANELS" : "FULL SCREEN");
        break;
    case SETTING_FRAME_RATE:
        snprintf(text, size, "FRAME RATE: %d FPS", settings->frame_rate);
        break;
    case SETTING_MODE:
        snprintf(text, size, "MODE: %s", director_mode_name(director->mode));
        break;
    case SETTING_PRESET_NAME:
        snprintf(text, size, "PRESET NAME: %s", settings->show_name ? "ON" : "OFF");
        break;
    case SETTING_LEVEL_METER:
        snprintf(text, size, "LEVEL METER: %s", settings->show_levels ? "ON" : "OFF");
        break;
    case SETTING_INTERVAL:
        snprintf(text, size, "PRESET INTERVAL: %u SEC", (unsigned)director->duration);
        break;
    case SETTING_HARD_CUTS:
        snprintf(text, size, "HARD CUTS: %s", director->hard_cuts ? "ON" : "OFF");
        break;
    case SETTING_VARIATION:
        snprintf(text, size, "INTERVAL VARIATION: %u SEC", director->variation);
        break;
    default:

        if (size)
        {
            text[0] = 0;
        }

        break;
    }
}

void settings_open(AppSettings* settings, const CdPlaybackStatus* cd)
{
    settings->open          = 1;
    settings->page          = MENU_ROOT;
    settings->menu_row      = 0;
    settings->playback_mode = cd->mode == CD_PROGRAM ? PLAYBACK_PROGRAM : cd->repeat == CD_REPEAT_ONE ? PLAYBACK_REPEAT_CURRENT
                                                                      : cd->repeat == CD_REPEAT_ALL   ? PLAYBACK_REPEAT_ALL
                                                                      : cd->mode == CD_SHUFFLE        ? PLAYBACK_SHUFFLE
                                                                                                      : PLAYBACK_CONTINUE;
}

int settings_row_count(const AppSettings* settings, int cd_available, int listening)
{
    return settings->page == MENU_VISUALIZER ? SETTING_COUNT : settings->page == MENU_PLAYBACK ? (cd_available ? PLAYBACK_ROW_COUNT : 1)
                                                                                               : MENU_ROOT_COUNT - !!listening;
}

AudioTransportRequests settings_playback_update(AppSettings* settings, PlayerState* player, const CdPlaybackStatus* cd, unsigned pressed)
{
    AudioTransportRequests actions = { 0 };
    int                    count   = (cd && cd->present) ? PLAYBACK_ROW_COUNT : 1;

    if (pressed & (INPUT_UP | INPUT_DOWN))
    {
        settings->menu_row = (settings->menu_row + ((pressed & INPUT_UP) ? count - 1 : 1)) % count;
    }

    if (!(pressed & (INPUT_LEFT | INPUT_RIGHT | INPUT_CROSS)))
    {
        return actions;
    }

    int row = (cd && cd->present) ? settings->menu_row : PLAYBACK_SOUND_ROW;

    if (row == PLAYBACK_SOUND_ROW)
    {
        settings->muted = !settings->muted;
    }
    else if (row == PLAYBACK_MODE_ROW)
    {
        static const AudioTransportCommand commands[] = { AUDIO_TRANSPORT_CONTINUE, AUDIO_TRANSPORT_SHUFFLE, AUDIO_TRANSPORT_REPEAT_ONE, AUDIO_TRANSPORT_REPEAT_ALL, AUDIO_TRANSPORT_PROGRAM };
        int                                direction  = pressed & INPUT_LEFT ? -1 : 1;
        int                                mode       = (settings->playback_mode + PLAYBACK_MODE_COUNT + direction) % PLAYBACK_MODE_COUNT;

        if (mode == PLAYBACK_PROGRAM && !player->saved_count)
        {
            return actions;
        }

        settings->playback_mode                   = mode;
        actions.commands[actions.command_count++] = commands[mode];
    }
    else if (row == PLAYBACK_PROGRAM_ROW && (pressed & INPUT_CROSS))
    {
        player_begin_program(player, cd);

        settings->open = 0;
    }
    else if (row == PLAYBACK_TIME_ROW)
    {
        player->time_remaining = !player->time_remaining;
    }

    return actions;
}

void settings_playback_format(const AppSettings* settings, const PlayerState* player, int row, char* text, size_t size)
{
    if (row == PLAYBACK_SOUND_ROW)
    {
        snprintf(text, size, "SOUND: %s", settings->muted ? "OFF" : "ON");
    }
    else if (row == PLAYBACK_MODE_ROW)
    {
        snprintf(text, size, "MODE: %s", playback_names[settings->playback_mode]);
    }
    else if (row == PLAYBACK_PROGRAM_ROW)
    {
        snprintf(text, size, "PROGRAM TRACKS (%d SAVED)", player->saved_count);
    }
    else
    {
        snprintf(text, size, "TIME MODE: %s", player->time_remaining ? "REMAINING" : "ELAPSED");
    }
}
