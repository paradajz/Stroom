#pragma once

#include "ui/settings/controller.h"
#include <gsKit.h>

/** Draw settings and highlight the selected row. */
void ui_settings_draw(GSGLOBAL* gs, const AppSettings* settings, const Director* director, const PlayerState* player, int cd_available, int listening);
