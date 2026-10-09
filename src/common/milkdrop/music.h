#pragma once

#include "milkdrop/milk_audio.h"

#define MILK_SPECTRUM_POINTS (MILK_AUDIO_BINS / 2)

/**
 * @brief Source-independent audio features and shared clock; contains no scene geometry.
 */
typedef struct MusicFeatures
{
    MilkAudio milk_audio;                          /**< Reference-style MilkDrop analysis state. */
    float     time;                                /**< Shared elapsed visualization time in seconds. */
    unsigned  frame;                               /**< Shared visualization frame number. */
    float     relative[3];                         /**< Reference immediate band ratios. */
    float     attenuated[3];                       /**< Reference smoothed band ratios. */
    float     spectrum_left[MILK_SPECTRUM_POINTS]; /**< Left spectrum paired into bins for the classic spectrum waveform. */
    float     custom_wave[2][MILK_AUDIO_SAMPLES];  /**< Normalized stereo waveform input for custom equations. */
} MusicFeatures;

/**
 * @brief Reset MilkDrop audio history and the shared clock.
 *
 * @param s Music state to initialize.
 */
void music_init(MusicFeatures* s);

/**
 * @brief Update MilkDrop equation inputs and the shared visualization clock.
 *
 * @param s Feature state to update.
 * @param a Current PCM and levels.
 * @param seconds Elapsed time in seconds.
 * @param custom_spectrum Whether custom waves need fresh stereo spectra this frame.
 */
void music_step(MusicFeatures* s, const Audio* a, float seconds, int custom_spectrum);
