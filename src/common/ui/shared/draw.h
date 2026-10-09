#pragma once

#include "ui/shared/style.h"

/**
 * @brief Draw a filled rectangle using width and height.
 * @param gs Graphics context.
 * @param x Left coordinate in pixels.
 * @param top Top coordinate in pixels.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param color Packed GS color.
 */
void ui_rectangle(GSGLOBAL* gs, float x, float top, float width, float height, u64 color);

/**
 * @brief Draw scaled text with a named palette color.
 * @param gs Graphics context.
 * @param x Left coordinate in pixels.
 * @param top Top coordinate in pixels.
 * @param text Null-terminated display text.
 * @param scale Glyph scale.
 * @param color Named UI palette color.
 */
void ui_label(GSGLOBAL* gs, float x, float top, const char* text, float scale, UiColor color);

/**
 * @brief Draw a rectangular outline with explicit thickness.
 * @param gs Graphics context.
 * @param left Left coordinate in pixels.
 * @param top Top coordinate in pixels.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param color Named UI palette color.
 * @param thickness Border thickness in pixels.
 */
void ui_outline(GSGLOBAL* gs, float left, float top, float width, float height, UiColor color, float thickness);

/**
 * @brief Draw a player-control border at the standard thickness.
 * @param gs Graphics context.
 * @param left Left coordinate in pixels.
 * @param top Top coordinate in pixels.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param color Named UI palette color.
 */
void ui_border(GSGLOBAL* gs, float left, float top, float width, float height, UiColor color);
