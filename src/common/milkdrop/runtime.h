#pragma once

#include "milkdrop/director.h"

/**
 * @brief MilkDrop playback, analysis, and visualization clock.
 */
typedef struct
{
    Director      director; /**< Preset selection and transition scheduler. */
    MusicFeatures music;    /**< Shared music features and visualization clock. */
    int           feedback; /**< Nonzero to enable feedback. */
} MilkdropRuntime;

/**
 * @brief Initialize visualization state and preset scheduling.
 *
 * @param state State to initialize.
 * @param seed Preset random seed.
 */
void milkdrop_runtime_init(MilkdropRuntime* state, uint32_t seed);

/**
 * @brief Clear audio history while preserving the clock and preset state.
 *
 * @param state Visualizer state to reset.
 */
void milkdrop_runtime_reset_audio(MilkdropRuntime* state);

/**
 * @brief Advance audio features, presets and the visualization clock.
 *
 * @param state Visualizer to update.
 * @param audio Current audio snapshot.
 * @param dt Elapsed time in seconds.
 */
void milkdrop_runtime_step(MilkdropRuntime* state, const Audio* audio, float dt);
