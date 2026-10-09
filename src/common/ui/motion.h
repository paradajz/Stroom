#pragma once

#include <stdint.h>

/** Reversible slide state; clock arithmetic wraps like EE milliseconds. */
typedef struct
{
    float    position;
    uint32_t at;
    int      initialized;
} UiMotion;

/** @brief Advance visibility towards shown, returning a continuous eased fraction. */
float ui_motion_step(UiMotion* motion, int shown, uint32_t now);
