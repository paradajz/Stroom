#include "ui/waiting/view.h"
#include "ui/shared/draw.h"
#include "ui/shared/text.h"
#include "ui/shared/player_layout.h"
#include <stdio.h>

#define WAITING_TITLE_TOP   391
#define WAITING_HINT_TOP    416
#define WAITING_ADDRESS_TOP 442

void ui_waiting_draw(GSGLOBAL* gs, const AudioSourceStatus* source, UiScreen screen, float bottom_offset)
{
    const char* title = source->kind == AUDIO_SOURCE_ERROR ? "ERROR" : screen == UI_SCREEN_DETECTING ? "IDENTIFYING DISC"
                                                                                                     : "WAITING FOR AUDIO";

    int pending = source->kind == AUDIO_SOURCE_ERROR || source->kind == AUDIO_SOURCE_WAITING;

    if (pending && source->error[0])
    {
        title = source->error;
    }

    float scale     = UI_PLAYER_HEADING_SCALE;
    float width     = ui_text_width(title, scale);
    float available = DISPLAY_WIDTH - 32;

    if (width > available)
    {
        scale *= available / width;
        width = available;
    }

    ui_label(gs, (DISPLAY_WIDTH - width) / 2, ui_player_y(WAITING_TITLE_TOP) + bottom_offset, title, scale, UI_COLOR_TEXT);

    if (pending)
    {
        return;
    }

    const char* instruction = screen == UI_SCREEN_DETECTING ? "CHECKING FOR AN AUDIO CD" : "INSERT DISC OR CAST FROM YOUR DEVICE";

    ui_label(gs, (DISPLAY_WIDTH - ui_text_width(instruction, UI_PLAYER_TEXT_SCALE)) / 2, ui_player_y(WAITING_HINT_TOP) + bottom_offset, instruction, UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);

    char        address[sizeof(source->network_address) + sizeof("PS2 IP ADDRESS: ")];
    const char* connection = NULL;

    if (source->network_ready && source->network_address[0])
    {
        snprintf(address, sizeof(address), "PS2 IP ADDRESS: %s", source->network_address);

        connection = address;
    }
    else if (screen != UI_SCREEN_DETECTING)
    {
        connection = source->network_ready ? "GETTING NETWORK ADDRESS" : "NETWORK UNAVAILABLE";
    }

    if (connection)
    {
        ui_label(gs, (DISPLAY_WIDTH - ui_text_width(connection, UI_PLAYER_TEXT_SCALE)) / 2, ui_player_y(WAITING_ADDRESS_TOP) + bottom_offset, connection, UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);
    }
}
