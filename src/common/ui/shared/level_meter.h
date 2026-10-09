#pragma once

#include <stdint.h>

/**
 * @brief Advance stereo display smoothing once per UI frame.
 * @param rms Two linear full-scale RMS amplitudes.
 * @param active Nonzero for audible, nonscanning playback; zero clears the meters.
 * @param now_ms Wrapping monotonic milliseconds.
 */
void ui_level_update(const float rms[2], int active, uint32_t now_ms);

/**
 * @brief Read the smoothed level for one stereo channel.
 * @param channel Zero for left, one for right.
 * @param rms Receives the clamped -60..0 dBFS fraction, or zero if inactive or invalid.
 */
void ui_level_values(unsigned channel, float* rms);
