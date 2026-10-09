#pragma once

#include "ui/shared/geometry.h"

/** Shared letterbox and meter geometry, in reference coordinates unless noted. */
#define UI_REFERENCE_HEIGHT    480.0f
#define UI_HEADER_HEIGHT       104.0f
#define UI_FOOTER_TOP          376.0f
#define UI_PANEL_BORDER_HEIGHT 2.0f /* Screen pixels. */
#define UI_METER_TOP           120.0f
#define UI_METER_HEIGHT        240.0f
#define UI_METER_WIDTH         24.0f /* Screen pixels. */

/** Shared player typography and header alignment. */
#define UI_PLAYER_STATUS_TOP    73.0f
#define UI_PLAYER_LABEL_BYTES   64
#define UI_PLAYER_TEXT_SCALE    2.5f
#define UI_PLAYER_HEADER_LIFT   0.3f
#define UI_PLAYER_HEADING_SCALE 3.0f

/** Convert player reference coordinates to screen pixels. */
static inline float ui_player_y(float reference)
{
    return reference * DISPLAY_HEIGHT / UI_REFERENCE_HEIGHT;
}
