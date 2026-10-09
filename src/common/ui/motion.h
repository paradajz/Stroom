#pragma once

#include <stdint.h>

/** Reversible slide state; clock arithmetic wraps like EE milliseconds. */
typedef struct
{
    float    position;
    uint32_t at;
    int      initialized;
} UiMotion;

/**
 * @brief Advance panel visibility and return its eased position.
 * @param motion Animation state to initialize or update.
 * @param shown Nonzero to show panels, zero to hide them.
 * @param now Wrapping monotonic milliseconds.
 * @return Continuous eased visibility fraction from zero to one.
 */
float ui_motion_step(UiMotion* motion, int shown, uint32_t now);
