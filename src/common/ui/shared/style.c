#include "ui/shared/style.h"

u64 ui_color(UiColor color)
{
    static const unsigned char palette[][3] = {
        [UI_COLOR_TEXT]            = { 240, 244, 255 },
        [UI_COLOR_ACCENT]          = { 25, 211, 230 },
        [UI_COLOR_BACKGROUND]      = { 5, 6, 15 },
        [UI_COLOR_SURFACE]         = { 11, 16, 48 },
        [UI_COLOR_DIVIDER]         = { 42, 49, 112 },
        [UI_COLOR_TEXTURE_NEUTRAL] = { 128, 128, 128 }
    };

    return GS_SETREG_RGBAQ(palette[color][0], palette[color][1], palette[color][2], color == UI_COLOR_TEXTURE_NEUTRAL ? 128 : UI_OVERLAY_ALPHA, 0);
}
