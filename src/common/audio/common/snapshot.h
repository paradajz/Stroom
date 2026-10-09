#pragma once

#include "contracts/audio.h"
#include <stdint.h>

#define AUDIO_HISTORY  2048
#define AUDIO_STALE_MS 250

/**
 * @brief Published source-independent PCM history and levels for read-only consumers.
 */
typedef struct
{
    int16_t  history[AUDIO_HISTORY][2]; /**< Recent stereo PCM16 frames. */
    unsigned history_write;             /**< Next history slot to write. */
    unsigned history_count;             /**< Number of valid history frames. */
    int      active;                    /**< Nonzero while PCM input is active. */
    float    rms[2];                    /**< Stereo RMS amplitudes; 1 is full scale. */
    float    peak[2];                   /**< Stereo sample-peak amplitudes; 1 is full scale. */
} Audio;
