#pragma once

#include "ui/cd_player/controller.h"
#include <gsKit.h>

/** Draw CD progress and program editing; the frame renderer owns track details. */
void ui_cd_player_draw(GSGLOBAL* gs, const CdPlaybackStatus* cd, const PlayerState* player, float top_offset, float bottom_offset, float visible);
