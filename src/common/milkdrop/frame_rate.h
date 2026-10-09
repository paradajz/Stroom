#pragma once

#include "contracts/milkdrop.h"

/**
 * @brief Choose the highest menu frame-rate setting a preset qualifies for.
 *
 * The shared contract loader derives the cutoffs from display refresh timing
 * and rounds them to the saved score precision. C and host scripts consume the
 * same values: 29.97 and 59.94 FPS for the current 60000/1001 Hz display.
 * Compare as floats to match saved scores in generated preset initializers.
 * A score of 59.94 returns 60, a score of 45 returns 30, and a score of 25
 * returns 0 for the current display.
 * Returning 60 means the preset is eligible in both the 30 and 60 FPS modes;
 * returning 30 means it is eligible only in the 30 FPS mode.
 *
 * Evaluated at build time in the generated preset table. The app stores this
 * result and compares it with the selected menu setting during playback.
 *
 * @param fps Saved benchmark FPS, finite and nonnegative; zero if unmeasured.
 * @return MILKDROP_FPS_HIGH or MILKDROP_FPS_BASELINE if the preset qualifies;
 *         zero if it qualifies for neither setting, including unmeasured presets.
 */
#define MILK_FRAME_RATE_MAXIMUM(fps)                                                                                                       \
    ((fps) >= (float)MILKDROP_FPS_HIGH_MINIMUM ? MILKDROP_FPS_HIGH : (fps) >= (float)MILKDROP_FPS_BASELINE_MINIMUM ? MILKDROP_FPS_BASELINE \
                                                                                                                   : 0)
