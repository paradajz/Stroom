#pragma once

#include "audio/common/snapshot.h"

#define MILK_AUDIO_SAMPLES 576
#define MILK_AUDIO_BINS    512

/**
 * @brief Reference-style audio conditioning, independent of display spectrum bands.
 */
typedef struct
{
    float    immediate[3];                    /**< Immediate low/mid/high band energies. */
    float    average[3];                      /**< Smoothed low/mid/high band energies. */
    float    long_average[3];                 /**< Long-term low/mid/high normalization baselines. */
    float    relative[3];                     /**< Immediate band energies relative to long-term baselines. */
    float    attenuated[3];                   /**< Smoothed band energies relative to long-term baselines. */
    float    waveform[2][MILK_AUDIO_SAMPLES]; /**< Aligned stereo waveforms in reference amplitude units. */
    float    spectrum[2][MILK_AUDIO_BINS];    /**< Equalized stereo spectra; updated only when requested. */
    float    left_spectrum[MILK_AUDIO_BINS];  /**< Left spectrum computed after waveform alignment. */
    float    previous[2][MILK_AUDIO_SAMPLES]; /**< Previous unaligned stereo waveform samples. */
    unsigned offsets[2];                      /**< Per-channel alignment offsets into the previous waveform. */
    unsigned frames;                          /**< Warm-up frame count, capped at 50. */
} MilkAudio;

/**
 * @brief Update reference-style bands, spectra, and aligned waveforms.
 *
 * @param state Persistent analysis state; zero-initialize before first use.
 * @param audio Source PCM history.
 * @param seconds Elapsed time in seconds.
 * @param custom_spectrum Whether custom waves need fresh stereo spectra this frame.
 */
void milk_audio_step(MilkAudio* state, const Audio* audio, float seconds, int custom_spectrum);
