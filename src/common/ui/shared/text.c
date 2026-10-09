#include "ui/shared/text.h"
#include <float.h>
#include "ui/shared/style.h"
#include "ui/shared/font.h"

void ui_text_clipped(GSGLOBAL* gs, float x, float y, const char* s, u64 color, float scale, float left, float right)
{
    for (; *s; ++s, x += UI_TEXT_ADVANCE * scale)
    {
        if (x >= right)
        {
            break;
        }

        if (x + UI_TEXT_WIDTH * scale <= left)
        {
            continue;
        }

        const char* glyph = ui_text_glyph((unsigned char)*s);

        if (!glyph && *s != ' ')
        {
            glyph = ui_text_glyph('?');
        }

        if (glyph)
        {
            for (int i = 0; i < UI_TEXT_WIDTH * UI_TEXT_HEIGHT; ++i)
            {
                if (glyph[i] == '1')
                {
                    const int row   = i / UI_TEXT_WIDTH;
                    float     begin = x + (i % UI_TEXT_WIDTH) * scale;
                    float     end   = begin + scale;

                    if (begin < left)
                    {
                        begin = left;
                    }

                    if (end > right)
                    {
                        end = right;
                    }

                    if (begin < end)
                    {
                        gsKit_prim_sprite(gs, begin, y + row * scale, end, y + row * scale + scale, 1, color);
                    }
                }
            }
        }
    }
}

void ui_text_scaled(GSGLOBAL* gs, float x, float y, const char* s, u64 color, float scale)
{
    ui_text_clipped(gs, x, y, s, color, scale, -FLT_MAX, FLT_MAX);
}

void ui_number(GSGLOBAL* gs, float x, float y, const char* text, float scale)
{
    ui_text_scaled(gs, x, y, text, ui_color(UI_COLOR_TEXT), scale);
}
