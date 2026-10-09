#include "ui/shared/draw.h"
#include "ui/shared/text.h"

#define BORDER_THICKNESS 3.0f

void ui_rectangle(GSGLOBAL* gs, float x, float top, float width, float height, u64 color)
{
    gsKit_prim_sprite(gs, x, top, x + width, top + height, 1, color);
}

void ui_label(GSGLOBAL* gs, float x, float top, const char* text, float scale, UiColor color)
{
    ui_text_scaled(gs, x, top, text, ui_color(color), scale);
}

void ui_outline(GSGLOBAL* gs, float left, float top, float width, float height, UiColor color, float thickness)
{
    u64 packed = ui_color(color);

    ui_rectangle(gs, left, top, width, thickness, packed);
    ui_rectangle(gs, left, top + height - thickness, width, thickness, packed);
    ui_rectangle(gs, left, top, thickness, height, packed);
    ui_rectangle(gs, left + width - thickness, top, thickness, height, packed);
}

void ui_border(GSGLOBAL* gs, float left, float top, float width, float height, UiColor color)
{
    ui_outline(gs, left, top, width, height, color, BORDER_THICKNESS);
}
