#include "ui/shared/player_view.h"
#include <stdio.h>
#include "ui/shared/draw.h"
#include "ui/shared/text.h"
#include "ui/shared/player_layout.h"
#include "ui/shared/level_meter.h"
#include "ui/shared/logo.h"

#define METER_BORDER_WIDTH  2.0f
#define METER_SEGMENT_GAP   2.0f
#define METER_INSET         7
#define METER_BOTTOM_INSET  12
#define METER_CONTENT_INSET 34
#define METER_SEGMENTS      30
#define METER_SEGMENT_WIDTH 10
#define WAITING_LOGO_WIDTH  280.0f

void ui_player_meters_draw(GSGLOBAL* gs)
{
    float top = ui_player_y(UI_METER_TOP), height = ui_player_y(UI_METER_HEIGHT);

    for (unsigned channel = 0; channel < 2; ++channel)
    {
        float left = channel ? UI_SAFE_RIGHT - UI_METER_WIDTH : UI_SAFE_LEFT;

        ui_rectangle(gs, left, top, UI_METER_WIDTH, height, ui_color(UI_COLOR_BACKGROUND));
        ui_outline(gs, left, top, UI_METER_WIDTH, height, UI_COLOR_TEXT, METER_BORDER_WIDTH);
        ui_label(gs, left + (UI_METER_WIDTH - ui_text_width("L", UI_PLAYER_TEXT_SCALE)) / 2, top + METER_INSET, channel ? "R" : "L", UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);

        float rms;

        ui_level_values(channel, &rms);

        float bottom = top + height - METER_BOTTOM_INSET;
        float step   = (height - METER_CONTENT_INSET) / METER_SEGMENTS;

        for (int segment = 0; segment < METER_SEGMENTS; ++segment)
        {
            ui_rectangle(gs, left + METER_INSET, bottom - (segment + 1) * step, METER_SEGMENT_WIDTH, step - METER_SEGMENT_GAP, ui_color(segment < rms * METER_SEGMENTS ? UI_COLOR_TEXT : UI_COLOR_DIVIDER));
        }
    }
}

void ui_player_source_draw(GSGLOBAL* gs, const AudioSourceStatus* source, int waiting, int muted, float offset)
{
    const char* text;

    if (waiting)
    {
        if (source->kind == AUDIO_SOURCE_ERROR || (!source->cd.error[0] && !source->error[0]))
        {
            ui_logo_draw(gs, DISPLAY_WIDTH * 0.5f, ui_player_y(UI_HEADER_HEIGHT * 0.5f) + offset, WAITING_LOGO_WIDTH);

            return;
        }

        text = source->cd.error[0] ? "CD / ERROR" : "NETWORK / ERROR";
    }
    else if (source->kind == AUDIO_SOURCE_CD)
    {
        text = source->cd.error[0] ? "CD / ERROR" : !source->cd.tracks ? "CD / LOADING"
                                                : source->cd.playing   ? "CD / PLAYING"
                                                : source->cd.paused    ? "CD / PAUSED"
                                                                       : "CD / STOPPED";
    }
    else
    {
        text = source->error[0] ? "NETWORK / ERROR" : source->listening ? "NETWORK / LISTENING"
                                                                        : "NETWORK / STREAMING";
    }

    char status[UI_PLAYER_LABEL_BYTES];

    if (muted && !waiting && !source->listening)
    {
        snprintf(status, sizeof(status), "%s / MUTED", text);

        text = status;
    }

    ui_label(gs, (DISPLAY_WIDTH - ui_text_width(text, UI_PLAYER_TEXT_SCALE)) / 2, ui_player_y(UI_PLAYER_STATUS_TOP - UI_HEADER_HEIGHT * UI_PLAYER_HEADER_LIFT) + offset, text, UI_PLAYER_TEXT_SCALE, UI_COLOR_TEXT);
}
