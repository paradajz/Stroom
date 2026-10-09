#include "ui/settings/view.h"
#include <stdio.h>
#include "ui/shared/draw.h"
#include "ui/shared/player_layout.h"
#include "ui/shared/text.h"

#define SETTINGS_PADDING      16
#define SETTINGS_MARKER_SPACE 24
#define SETTINGS_TITLE_HEIGHT 40
#define SETTINGS_ROW_STEP     32

/**
 * @brief Format the label for one row of the active menu page.
 * @param settings Active settings page.
 * @param director Current preset state.
 * @param player Playback program draft.
 * @param cd_available Nonzero when CD options are available.
 * @param row Logical row index.
 * @param text Destination text buffer.
 * @param size Destination capacity in bytes.
 */
static void row_text(const AppSettings* settings, const Director* director, const PlayerState* player, int cd_available, int row, char* text, size_t size)
{
    if (settings->page == MENU_VISUALIZER)
    {
        settings_format(settings, director, (SettingRow)row, text, size);
    }
    else if (settings->page == MENU_PLAYBACK)
    {
        settings_playback_format(settings, player, cd_available ? row : PLAYBACK_SOUND_ROW, text, size);
    }
    else
    {
        const char* names[] = { "VISUALISER", "PLAYBACK", "EXIT" };

        snprintf(text, size, "%s", names[row]);
    }
}

/**
 * @brief Map a visible root-menu row around unavailable playback settings.
 * @param settings Active menu page.
 * @param row Visible row index.
 * @param listening Nonzero for a silent listening session.
 * @return Logical settings row index.
 */
static int item_row(const AppSettings* settings, int row, int listening)
{
    return settings->page == MENU_ROOT && listening && row >= MENU_PLAYBACK_ROW ? row + 1 : row;
}

void ui_settings_draw(GSGLOBAL* gs, const AppSettings* settings, const Director* director, const PlayerState* player, int cd_available, int listening)
{
    int         count         = settings_row_count(settings, cd_available, listening);
    const char* title         = settings->page == MENU_ROOT ? "MENU" : settings->page == MENU_VISUALIZER ? "VISUALISER"
                                                                                                         : "PLAYBACK";
    float       content_width = ui_text_width(title, UI_PLAYER_HEADING_SCALE);
    char        text[UI_PLAYER_LABEL_BYTES];

    for (int row = 0; row < count; ++row)
    {
        row_text(settings, director, player, cd_available, item_row(settings, row, listening), text, sizeof(text));

        float row_width = SETTINGS_MARKER_SPACE + ui_text_width(text, UI_PLAYER_TEXT_SCALE);

        if (row_width > content_width)
        {
            content_width = row_width;
        }
    }

    float width       = content_width + 2 * SETTINGS_PADDING;
    float height      = 2 * SETTINGS_PADDING + SETTINGS_TITLE_HEIGHT + (count - 1) * SETTINGS_ROW_STEP + UI_TEXT_HEIGHT * UI_PLAYER_TEXT_SCALE;
    float left        = (DISPLAY_WIDTH - width) / 2;
    float top         = (DISPLAY_HEIGHT - height) / 2;
    float marker_left = left + SETTINGS_PADDING;
    float row_top     = top + SETTINGS_PADDING + SETTINGS_TITLE_HEIGHT;

    ui_rectangle(gs, left, top, width, height, ui_color(UI_COLOR_BACKGROUND));
    ui_border(gs, left, top, width, height, UI_COLOR_ACCENT);
    ui_label(gs, marker_left, top + SETTINGS_PADDING, title, UI_PLAYER_HEADING_SCALE, UI_COLOR_TEXT);

    for (int row = 0; row < count; ++row)
    {
        int selected = settings->page == MENU_VISUALIZER ? (int)settings->row : settings->menu_row;

        row_text(settings, director, player, cd_available, item_row(settings, row, listening), text, sizeof(text));

        if (selected == item_row(settings, row, listening))
        {
            ui_label(gs, marker_left, row_top + row * SETTINGS_ROW_STEP, ">", UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);
        }

        ui_label(gs, marker_left + SETTINGS_MARKER_SPACE, row_top + row * SETTINGS_ROW_STEP, text, UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);
    }
}
