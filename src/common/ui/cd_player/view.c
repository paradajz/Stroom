#include "ui/cd_player/view.h"
#include "ui/shared/draw.h"
#include "ui/shared/text.h"
#include "ui/shared/player_layout.h"
#include "ui/cd_player/navigation.h"
#include "util/time_units.h"
#include <math.h>
#include <stdio.h>

#define GRID_LEFT              91
#define GRID_STEP              58
#define GRID_WIDTH             52
#define GRID_TOP               128
#define GRID_ROW_STEP          108
#define GRID_HEIGHT            100
#define HEADER_NUMBER_SCALE    6.5f
#define GRID_LINK_SCALE        4.0f
#define GRID_NUMBER_SCALE      5.5f
#define PROGRESS_LEFT          104
#define PROGRESS_WIDTH         432
#define PROGRESS_HEIGHT        6
#define PROGRESS_CURSOR_HEIGHT 12
#define PROGRESS_CURSOR_WIDTH  4
#define TRACK_COUNT_LEFT       112
#define TRACK_PROGRESS_TOP     68
#define DISC_PROGRESS_TOP      86
#define PROGRAM_HINT_TOP       451

static void progress(GSGLOBAL* gs, float top, float fraction)
{
    fraction = fmaxf(0, fminf(1, fraction));

    ui_rectangle(gs, PROGRESS_LEFT, top, PROGRESS_WIDTH, PROGRESS_HEIGHT, ui_color(UI_COLOR_DIVIDER));
    ui_rectangle(gs, PROGRESS_LEFT, top, PROGRESS_WIDTH * fraction, PROGRESS_HEIGHT, ui_color(UI_COLOR_ACCENT));
    ui_rectangle(gs, PROGRESS_LEFT + (PROGRESS_WIDTH - PROGRESS_CURSOR_WIDTH / 2.0f) * fraction, top - PROGRESS_HEIGHT / 2.0f, PROGRESS_CURSOR_WIDTH, PROGRESS_CURSOR_HEIGHT, ui_color(UI_COLOR_TEXT));
}

static void program_track_box(GSGLOBAL* gs, float left, float top, int selected, int focused)
{
    float height = ui_player_y(GRID_HEIGHT);

    ui_rectangle(gs, left, top, GRID_WIDTH, height, ui_color(selected ? UI_COLOR_SURFACE : UI_COLOR_BACKGROUND));
    ui_border(gs, left, top, GRID_WIDTH, height, selected ? UI_COLOR_ACCENT : focused ? UI_COLOR_TEXT
                                                                                      : UI_COLOR_DIVIDER);
}

static void progress_row(GSGLOBAL* gs, float top, float fraction, const char* label, unsigned seconds)
{
    float label_top = top + PROGRESS_HEIGHT / 2.0f - UI_TEXT_HEIGHT * UI_PLAYER_TEXT_SCALE / 2;

    progress(gs, top, fraction);
    ui_label(gs, UI_SAFE_LEFT, label_top, label, UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);

    char text[UI_PLAYER_LABEL_BYTES];

    snprintf(text, sizeof(text), "%02u:%02u", seconds / SECONDS_PER_MINUTE, seconds % SECONDS_PER_MINUTE);
    ui_label(gs, UI_SAFE_RIGHT - ui_text_width(text, UI_PLAYER_TEXT_SCALE), label_top, text, UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);
}

static void cd_header(GSGLOBAL* gs, const CdPlaybackStatus* cd, const PlayerState* player, float offset)
{
    float header_offset = offset - ui_player_y(UI_HEADER_HEIGHT) * UI_PLAYER_HEADER_LIFT;
    float number_top    = ui_player_y(UI_PLAYER_STATUS_TOP) + header_offset + UI_TEXT_HEIGHT * (UI_PLAYER_TEXT_SCALE - HEADER_NUMBER_SCALE);
    char  text[UI_PLAYER_LABEL_BYTES];

    snprintf(text, sizeof(text), "%02d", cd->track);
    ui_number(gs, UI_SAFE_LEFT, number_top, text, HEADER_NUMBER_SCALE);
    snprintf(text, sizeof(text), "/ %02d", cd->tracks);
    ui_label(gs, TRACK_COUNT_LEFT, number_top + UI_TEXT_HEIGHT * (HEADER_NUMBER_SCALE - UI_PLAYER_HEADING_SCALE), text, UI_PLAYER_HEADING_SCALE, UI_COLOR_TEXT);

    unsigned seconds = player->time_remaining ? (cd->duration_seconds > cd->elapsed_seconds ? cd->duration_seconds - cd->elapsed_seconds : 0) : cd->elapsed_seconds;

    snprintf(text, sizeof(text), "%02u:%02u", seconds / SECONDS_PER_MINUTE, seconds % SECONDS_PER_MINUTE);

    float scale = HEADER_NUMBER_SCALE;
    float left  = UI_SAFE_RIGHT - ui_text_width(text, scale);

    if (player->time_remaining)
    {
        ui_number(gs, left - UI_TEXT_ADVANCE * scale, number_top, "-", scale);
    }

    ui_number(gs, left, number_top, text, scale);
    progress_row(gs, ui_player_y(TRACK_PROGRESS_TOP) + offset, cd->track_progress, "TRACK", cd->duration_seconds);
    progress_row(gs, ui_player_y(DISC_PROGRESS_TOP) + offset, cd->disc_progress, "DISC", cd->disc_duration_seconds);
}

static void track_grid(GSGLOBAL* gs, const CdPlaybackStatus* cd, const PlayerState* player, float visible)
{
    if (!player->editing)
    {
        return;
    }

    PlayerPage page   = player_page(cd->tracks, player->page);
    float      offset = DISPLAY_HEIGHT * (1 - visible);

    for (int slot = 0; slot < page.slots; ++slot)
    {
        int track = player_page_track(page, slot);

        if (!track)
        {
            continue;
        }

        int   row      = slot / PLAYER_GRID_COLUMNS;
        float left     = GRID_LEFT + (slot % PLAYER_GRID_COLUMNS) * GRID_STEP;
        float top      = ui_player_y(GRID_TOP + row * GRID_ROW_STEP) + offset;
        int   selected = player_track_programmed(player, track);
        int   focused  = player->slot == slot;

        program_track_box(gs, left, top, selected, focused);

        char text[UI_PLAYER_LABEL_BYTES];

        if (track < 0)
        {
            snprintf(text, sizeof(text), "...");
        }
        else
        {
            snprintf(text, sizeof(text), "%02d", track);
        }

        float scale = track < 0 ? GRID_LINK_SCALE : GRID_NUMBER_SCALE;

        ui_text_scaled(gs, left + (GRID_WIDTH - ui_text_width(text, scale)) / 2, top + (ui_player_y(GRID_HEIGHT) - UI_TEXT_HEIGHT * scale) / 2, text, ui_color(UI_COLOR_TEXT), scale);
    }
}

static void cd_footer(GSGLOBAL* gs, const PlayerState* player, float offset)
{
    char text[UI_PLAYER_LABEL_BYTES];

    if (player->editing)
    {
        snprintf(text, sizeof(text), "%02d SELECTED", player->count);
        ui_label(gs, UI_SAFE_LEFT, ui_player_y(PROGRAM_HINT_TOP) + offset, "SQUARE: CONFIRM  TRIANGLE: CANCEL", UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);
        ui_label(gs, UI_SAFE_RIGHT - ui_text_width(text, UI_PLAYER_TEXT_SCALE), ui_player_y(PROGRAM_HINT_TOP) + offset, text, UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);
        return;
    }
}

void ui_cd_player_draw(GSGLOBAL* gs, const CdPlaybackStatus* cd, const PlayerState* player, float top_offset, float bottom_offset, float visible)
{
    cd_header(gs, cd, player, top_offset);
    track_grid(gs, cd, player, visible);
    cd_footer(gs, player, bottom_offset);
}
