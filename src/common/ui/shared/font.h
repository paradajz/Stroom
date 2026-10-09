#pragma once

#define UI_TEXT_WIDTH   3
#define UI_TEXT_HEIGHT  5
#define UI_TEXT_GAP     1
#define UI_TEXT_ADVANCE (UI_TEXT_WIDTH + UI_TEXT_GAP)

/**
 * @brief Resolve a supported byte to its uppercase bitmap.
 * @param ch Display character byte.
 * @return Borrowed glyph bitmap, or NULL for space and unsupported bytes.
 */
const char* ui_text_glyph(unsigned char ch);
