#pragma once

#include <gsKit.h>
#include "ui/shared/geometry.h"

#define UI_OVERLAY_ALPHA 115

/** Named colors used by player screens and visualizer overlays. */
typedef enum
{
    UI_COLOR_TEXT,
    UI_COLOR_ACCENT,
    UI_COLOR_BACKGROUND,
    UI_COLOR_SURFACE,
    UI_COLOR_DIVIDER,
    UI_COLOR_TEXTURE_NEUTRAL
} UiColor;

/** @brief Read a named color from the shared UI palette. */
u64 ui_color(UiColor color);
