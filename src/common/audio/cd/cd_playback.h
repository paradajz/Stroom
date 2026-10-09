#pragma once

#include "audio/cd/cd_format.h"
#include <stdint.h>

/** Failure codes for this API. */
typedef enum
{
    CD_PLAYBACK_ERROR_PROGRAM_SIZE    = -1,
    CD_PLAYBACK_ERROR_TRACK_RANGE     = -2,
    CD_PLAYBACK_ERROR_DUPLICATE_TRACK = -3,
} CdPlaybackError;

typedef enum
{
    CD_CONTINUE,
    CD_SHUFFLE,
    CD_PROGRAM
} CdPlaybackMode;

typedef enum
{
    CD_REPEAT_OFF,
    CD_REPEAT_ONE,
    CD_REPEAT_ALL
} CdRepeat;

/**
 * @brief Track order and repeat policy, independent of drive I/O.
 */
typedef struct
{
    CdPlaybackMode mode;                   /**< Continue, shuffle, or programmed playback. */
    CdRepeat       repeat;                 /**< End-of-track repeat policy. */
    int            order[CD_MAX_TRACKS];   /**< Ordered one-based track numbers. */
    int            count;                  /**< Number of valid entries in order. */
    int            program[CD_MAX_TRACKS]; /**< Saved program survives other playback modes. */
    unsigned       program_count;
    uint32_t       random; /**< Mutable shuffle RNG state; zero is seeded on first use. */
} CdPlayback;

/**
 * @brief Build sequential or shuffled track order, keeping the current track first in shuffle.
 *
 * @param p Playback state to update.
 * @param mode Requested playback mode.
 * @param tracks Disc track count, 0..CD_MAX_TRACKS.
 * @param current Current one-based track number.
 */
void cd_playback_mode(CdPlayback* p, CdPlaybackMode mode, int tracks, int current);

/**
 * @brief Validate and install a program of unique tracks.
 *
 * @param p Playback state, unchanged on failure.
 * @param tracks Ordered one-based track numbers.
 * @param count Number of entries, 1..99.
 * @param total Disc track count.
 * @return 0 on success, a negative CdPlaybackError on failure.
 */
int cd_playback_program(CdPlayback* p, const int* tracks, unsigned count, int total);

/**
 * @brief Resolve the next track using the current order and repeat policy.
 *
 * @param p Playback order and repeat settings.
 * @param current Current one-based track number.
 * @param direction Negative selects previous; otherwise next.
 * @param automatic Nonzero for an end-of-track advance; zero for manual navigation.
 * @return One-based track, or 0 to stop; manual navigation stays at an unwrapped boundary.
 */
int cd_playback_next(CdPlayback* p, int current, int direction, int automatic);
