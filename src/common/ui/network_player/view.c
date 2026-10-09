#include "ui/network_player/view.h"
#include "ui/shared/draw.h"
#include "ui/shared/text.h"
#include "ui/shared/player_layout.h"
#include "ui/shared/track_view.h"

#define LISTENING_LABEL_TOP 421
#define LISTENING_PREFIX    "LISTENING FROM: "

void ui_network_listening_draw(GSGLOBAL* gs, const char* device_name, float bottom_offset)
{
    if (!device_name[0])
    {
        return;
    }

    char     text[UI_PLAYER_LABEL_BYTES] = LISTENING_PREFIX;
    unsigned prefix                      = sizeof(LISTENING_PREFIX) - 1;
    unsigned limit                       = (unsigned)((UI_SAFE_RIGHT - UI_SAFE_LEFT) / (UI_TEXT_ADVANCE * UI_PLAYER_TEXT_SCALE));

    if (limit >= sizeof(text))
    {
        limit = sizeof(text) - 1;
    }

    ui_track_text_fit(text + prefix, device_name, limit - prefix);
    ui_label(gs, (DISPLAY_WIDTH - ui_text_width(text, UI_PLAYER_TEXT_SCALE)) / 2, ui_player_y(LISTENING_LABEL_TOP) + bottom_offset, text, UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);
}
