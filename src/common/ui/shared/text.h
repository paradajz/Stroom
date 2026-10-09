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

/** Draw text clipped horizontally to the supplied pixel bounds. */
void ui_text_clipped(GSGLOBAL* gs, float x, float y, const char* s, u64 color, float scale, float left, float right);

/** @brief Draw display numbers using the shared font and text palette color. */
void ui_number(GSGLOBAL* gs, float x, float y, const char* text, float scale);
