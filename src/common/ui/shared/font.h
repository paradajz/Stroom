#pragma once

#define UI_TEXT_WIDTH   3
#define UI_TEXT_HEIGHT  5
#define UI_TEXT_GAP     1
#define UI_TEXT_ADVANCE (UI_TEXT_WIDTH + UI_TEXT_GAP)

/** @brief Return the uppercase bitmap for a supported byte; space and unsupported bytes return NULL. */
const char* ui_text_glyph(unsigned char ch);
