#pragma once

#include <gsKit.h>
#include "milkdrop/director.h"

/**
 * @brief Draw the wrapped preset name at the requested font scale.
 * @param gs Graphics context.
 * @param director Current preset selection.
 * @param scale Glyph scale.
 */
void ui_preset_name_scaled(GSGLOBAL* gs, const Director* director, float scale);
