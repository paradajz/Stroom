#pragma once

#include "ui/cd_player/controller.h"

#define PLAYER_GRID_COLUMNS 8
#define PLAYER_GRID_SLOTS   (PLAYER_GRID_COLUMNS * 2)

/**
 * @brief Find the sliding grid-window index beginning at a track's row.
 * @param track One-based track number; no disc-length validation is performed.
 * @return Zero-based row/window index, or zero for nonpositive track numbers.
 */
int player_page_index(int track);

/**
 * @brief Advance the program grid by one row, wrapping after the final window.
 * @param player Navigation state; updates page and resets slot to zero.
 * @param tracks Disc track count; player_page() applies the supported bounds.
 */
void player_page_turn(PlayerState* player, int tracks);

/**
 * @brief Move program-grid focus without issuing playback commands.
 *
 * Does nothing outside program editing. Directional input may change the window;
 * focus is clamped to its slots and skips unused slots to the navigation link.
 *
 * @param player Navigation state to update.
 * @param cd Non-NULL disc snapshot supplying the track count while editing.
 * @param pressed Newly pressed INPUT_* direction bits.
 */
void player_navigate(PlayerState* player, const CdPlaybackStatus* cd, unsigned pressed);
