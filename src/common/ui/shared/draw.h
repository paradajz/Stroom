#pragma once

#include "ui/shared/style.h"

/** Draw a rectangle using width and height. */
void ui_rectangle(GSGLOBAL* gs, float x, float top, float width, float height, u64 color);

/** Draw scaled text using a named palette color. */
void ui_label(GSGLOBAL* gs, float x, float top, const char* text, float scale, UiColor color);

/** Draw an outline with the requested thickness. */
void ui_outline(GSGLOBAL* gs, float left, float top, float width, float height, UiColor color, float thickness);

/** Draw a standard player control border. */
void ui_border(GSGLOBAL* gs, float left, float top, float width, float height, UiColor color);
