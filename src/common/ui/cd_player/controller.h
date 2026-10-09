#pragma once

#include "audio/cd/cd_status.h"
#include "audio/common/transport.h"
#include "platform/input/input.h"

/** Special grid result; positive values identify tracks and zero means an empty slot. */
typedef enum
{
    PLAYER_PAGE_NEXT = -2,
} PlayerPageResult;

#define PLAYER_SCAN_HOLD_MS 300u

/**
 * @brief Program-grid layout for up to 16 slots, including an optional next-window link.
 */
typedef struct
{
    int first; /**< One-based first track on the page. */
    int count; /**< Number of tracks in the window, at most 16. */
    int next;  /**< Nonzero if a next-page slot is present. */
    int slots; /**< Total track and navigation slots. */
} PlayerPage;

/**
 * @brief One pending tap-or-hold transport gesture.
 */
typedef struct
{
    int      direction; /**< -1 previous/rewind, 1 next/forward, or 0 inactive. */
    uint32_t started;   /**< Press timestamp in monotonic milliseconds. */
} PlayerGesture;

/**
 * @brief CD player focus, time selection, and editable program draft.
 */
typedef struct
{
    PlayerGesture shoulder;       /**< Physical shoulder-button gesture state. */
    int           page;           /**< Zero-based track page index. */
    int           slot;           /**< Zero-based track-row slot index. */
    int           time_remaining; /**< Nonzero to display remaining track time. */
    int           editing;        /**< Nonzero while editing a program. */
    int           count;          /**< Number of tracks in the program draft. */
    int           saved_count;
    int           saved_tracks[CD_MAX_TRACKS];
    int           tracks[CD_MAX_TRACKS]; /**< Ordered one-based tracks in the program draft. */
} PlayerState;

/** @brief Whether the one-based track belongs to the program draft. */
int player_track_programmed(const PlayerState* player, int track);

/**
 * @brief Describe a page of up to 16 slots including a next-window link.
 *
 * @param tracks Disc track count, capped at 99.
 * @param page Zero-based page index, clamped to available pages.
 * @return Page layout, or an empty layout when no tracks exist.
 */
PlayerPage player_page(int tracks, int page);

/**
 * @brief Resolve a track-row slot into a track or navigation action.
 *
 * @param page Page layout.
 * @param slot Zero-based slot index.
 * @return One-based track, PLAYER_PAGE_NEXT for the next window, or 0 for an invalid slot.
 */
int player_page_track(PlayerPage page, int slot);

/**
 * @brief Reset player navigation, gestures, and the program draft.
 *
 * @param player Player state to initialize.
 */
void player_init(PlayerState* player);

/** Open a draft from the last confirmed program. */
void player_begin_program(PlayerState* player, const CdPlaybackStatus* cd);

/**
 * @brief Handle player input; inactive input cancels scanning but preserves the draft.
 * Start toggles playback. During program editing, Cross toggles tracks, Square confirms, and Triangle cancels.
 *
 * @param player Player state to update; reinitialize on disc changes.
 * @param cd Current disc snapshot.
 * @param pressed Newly pressed INPUT_* bits.
 * @param held Currently held INPUT_* bits.
 * @param active Nonzero when the player owns input.
 * @param now_ms Current monotonic time in milliseconds.
 * @return Transport commands, a submitted program, and scan direction.
 */
AudioTransportRequests player_update(PlayerState* player, const CdPlaybackStatus* cd, unsigned pressed, unsigned held, int active, uint32_t now_ms);
