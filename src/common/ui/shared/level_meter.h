#pragma once

#include <stdint.h>

/** @brief Advance stereo meter display smoothing once per UI frame. */
void ui_level_update(const float rms[2], int active, uint32_t now_ms);

/** @brief Read smoothed -60..0 dBFS fractions for a stereo channel. */
void ui_level_values(unsigned channel, float* rms);
