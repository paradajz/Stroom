#pragma once

#include "milkdrop/director.h"
#include "ui/cd_player/controller.h"
#include "platform/input/input.h"
#include <stddef.h>

typedef enum
{
    SETTING_DISPLAY,
    SETTING_FRAME_RATE,
    SETTING_MODE,
    SETTING_PRESET_NAME,
    SETTING_LEVEL_METER,
    SETTING_INTERVAL,
    SETTING_VARIATION,
    SETTING_HARD_CUTS,
    SETTING_COUNT
} SettingRow;

typedef enum
{
    PLAYBACK_CONTINUE,
    PLAYBACK_SHUFFLE,
    PLAYBACK_REPEAT_CURRENT,
    PLAYBACK_REPEAT_ALL,
    PLAYBACK_PROGRAM,
    PLAYBACK_MODE_COUNT
} PlaybackMenuMode;

typedef enum
{
    MENU_ROOT,
    MENU_VISUALIZER,
    MENU_PLAYBACK
} MenuPage;

enum
{
    MENU_VISUALIZER_ROW,
    MENU_PLAYBACK_ROW,
    MENU_EXIT_ROW,
    MENU_ROOT_COUNT
};

enum
{
    PLAYBACK_MODE_ROW,
    PLAYBACK_PROGRAM_ROW,
    PLAYBACK_TIME_ROW,
    PLAYBACK_SOUND_ROW,
    PLAYBACK_ROW_COUNT
};

/**
 * @brief Runtime settings-menu state and optional visual overlays.
 */
typedef struct
{
    MenuPage         page;
    int              menu_row;
    PlaybackMenuMode playback_mode;
    int              muted;       /**< Nonzero to mute sound without stopping playback or analysis. */
    int              open;        /**< Nonzero when the settings menu is visible. */
    int              show_panels; /**< Nonzero for the player panels; zero for fullscreen. */
    int              show_name;   /**< Nonzero to show the preset-name overlay. */
    int              show_levels; /**< Nonzero to show the level-meter overlay. */
    int              frame_rate;  /**< Nominal presentation cap: 30 or 60 FPS. */
    SettingRow       row;         /**< Currently selected settings row. */
} AppSettings;

/**
 * @brief Apply visualizer-menu navigation and edits while settings owns input.
 *
 * A frame-rate change with no eligible presets leaves the selected rate unchanged.
 *
 * @param settings Menu state to update.
 * @param director Preset settings to edit.
 * @param pressed Newly pressed INPUT_* bits.
 */
void settings_update(AppSettings* settings, Director* director, unsigned pressed);

/**
 * @brief Format one settings row for display.
 *
 * @param settings Menu settings.
 * @param director Preset scheduling settings.
 * @param row Row to format.
 * @param text Destination string; empty for an unknown row.
 * @param size Buffer capacity in bytes, including the terminator.
 */
void settings_format(const AppSettings* settings, const Director* director, SettingRow row, char* text, size_t size);

/**
 * @brief Apply playback-menu input and return commands for the caller to dispatch.
 *
 * Updates sound/time preferences locally. Opening program editing copies the saved
 * program into the editable draft and closes the menu. Selecting Program without a saved
 * program leaves the mode unchanged. This function performs no device operations.
 *
 * @param settings Active playback-menu state to update.
 * @param player Player preferences and saved program to inspect or update.
 * @param cd Disc snapshot, or NULL for sender-controlled network playback.
 *           Without a present disc, only Sound is editable.
 * @param pressed Newly pressed INPUT_* bits.
 * @return Value-owned playback actions; an empty result may still accompany local edits.
 */
AudioTransportRequests settings_playback_update(AppSettings* settings, PlayerState* player, const CdPlaybackStatus* cd, unsigned pressed);

/**
 * @brief Format one playback-menu row into caller-owned storage.
 * @param settings Settings with a valid playback_mode.
 * @param player Player time preference and saved-program count.
 * @param row PLAYBACK_*_ROW value; unrecognized values format the time preference.
 * @param text Destination; truncated and NUL-terminated when size is nonzero.
 * @param size Buffer capacity including the terminator; zero writes nothing.
 */
void settings_playback_format(const AppSettings* settings, const PlayerState* player, int row, char* text, size_t size);

/**
 * @brief Open the root menu and synchronize its playback mode from CD status.
 * @param settings Menu state; resets page and menu_row, preserving other preferences.
 * @param cd Non-NULL snapshot; use an empty snapshot when no CD is selected.
 */
void settings_open(AppSettings* settings, const CdPlaybackStatus* cd);

/**
 * @brief Count visible rows for the selected menu page.
 * @param settings Menu state supplying the page.
 * @param cd_available Nonzero to expose CD playback rows; otherwise Sound only.
 * @param listening Nonzero to omit Playback from the root menu. The caller must
 *                  leave the Playback page when entering listening mode.
 * @return Visible row count; hidden root rows retain their original row IDs.
 */
int settings_row_count(const AppSettings* settings, int cd_available, int listening);
