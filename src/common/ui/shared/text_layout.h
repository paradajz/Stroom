#pragma once

#include "ui/shared/font.h"

/**
 * @brief Measure bitmap text without the final inter-character space.
 * @param text Null-terminated text in the renderer's single-byte font.
 * @param scale Glyph scale multiplier.
 * @return Visible width in pixels, or zero for empty text.
 */
float ui_text_width(const char* text, float scale);
