#pragma once

#include "platform/graphics/display_config.h"

/* MilkDrop geometry derived from the selected display dimensions. These are
 * independent of audio sample counts and reference-effect texture tiers. */
#define MILK_VIEWPORT_HALF_WIDTH  (DISPLAY_WIDTH / 2.0f)
#define MILK_VIEWPORT_HALF_HEIGHT (DISPLAY_HEIGHT / 2.0f)
#define MILK_VIEWPORT_ASPECT      ((float)DISPLAY_HEIGHT / DISPLAY_WIDTH)
