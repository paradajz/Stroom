#include "ui/visualizer/overlays.h"
#include "ui/shared/text.h"
#include "ui/shared/style.h"
#include <string.h>

#define PRESET_COLUMNS  44
#define PRESET_TOP      44
#define PRESET_LINE_GAP 6

/**
 * @brief Draw a preset name wrapped within the TV-safe width.
 *
 * Wrap at 44 columns without truncating the name or drawing a background panel.
 *
 * @param gs GS drawing context.
 * @param y First line top in pixels.
 * @param name Complete preset name.
 * @param color Packed GS color.
 * @param scale Font cell scale.
 */
static void preset_label(GSGLOBAL* gs, float y, const char* name, u64 color, float scale)
{
    while (*name)
    {
        size_t length = strlen(name), count = length < PRESET_COLUMNS ? length : PRESET_COLUMNS;

        if (length > count && name[count] != ' ')
        {
            size_t word = count;

            while (word && name[word] != ' ')
            {
                --word;
            }

            if (word)
            {
                count = word;
            }
        }

        char line[PRESET_COLUMNS + 1];

        memcpy(line, name, count);

        line[count] = 0;

        ui_text_scaled(gs, UI_SAFE_LEFT, y, line, color, scale);

        y += (UI_TEXT_HEIGHT + 1) * scale + PRESET_LINE_GAP;
        name += count;

        while (*name == ' ')
        {
            ++name;
        }
    }
}

void ui_preset_name_scaled(GSGLOBAL* gs, const Director* director, float scale)
{
    const char* name  = preset_name(director->current.kind);
    u64         white = ui_color(UI_COLOR_TEXT);

    preset_label(gs, PRESET_TOP, name, white, scale);
}
