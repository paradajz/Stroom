#pragma once

#include <gsKit.h>

/**
 * @brief Draw the logo without its tagline, preserving its aspect ratio.
 *
 * Queues static geometry using the context's current alpha blending settings.
 * Height is derived from width using the 952 by 264 logo canvas.
 *
 * @param gs Initialized GS drawing context, used on the renderer thread.
 * @param center_x Horizontal center of the logo in screen pixels.
 * @param center_y Vertical center of the logo in screen pixels.
 * @param width Positive logo width in screen pixels.
 */
void ui_logo_draw(GSGLOBAL* gs, float center_x, float center_y, float width);
