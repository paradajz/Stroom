#pragma once

#include <gsKit.h>
#include "ui/shared/text_layout.h"

/**
 * @brief Draw uppercase bitmap text at the requested scale; unsupported bytes use a question mark.
 *
 * @param gs GS drawing context.
 * @param x Left position in pixels.
 * @param y Top position in pixels.
 * @param s Null-terminated text.
 * @param color Packed GS color.
 * @param scale Glyph scale multiplier.
 */
void ui_text_scaled(GSGLOBAL* gs, float x, float y, const char* s, u64 color, float scale);

/**
 * @brief Draw bitmap text clipped horizontally to pixel bounds.
 * @param gs Graphics context.
 * @param x Left text origin in pixels.
 * @param y Top text origin in pixels.
 * @param s Null-terminated display text.
 * @param color Packed GS color.
 * @param scale Glyph scale.
 * @param left Inclusive left clipping bound in pixels.
 * @param right Right clipping bound in pixels.
 */
void ui_text_clipped(GSGLOBAL* gs, float x, float y, const char* s, u64 color, float scale, float left, float right);

/**
 * @brief Draw numeric display text using the shared font and text color.
 * @param gs Graphics context.
 * @param x Left origin in pixels.
 * @param y Top origin in pixels.
 * @param text Null-terminated numeric text.
 * @param scale Glyph scale.
 */
void ui_number(GSGLOBAL* gs, float x, float y, const char* text, float scale);
