#pragma once

#include "ui/cd_player/controller.h"
#include <gsKit.h>

/**
 * @brief Draw CD progress and program editing controls.
 * The frame renderer draws shared track details separately.
 * @param gs Graphics context.
 * @param cd Current disc and playback snapshot.
 * @param player Navigation and program-editing state.
 * @param top_offset Top panel animation offset in pixels.
 * @param bottom_offset Bottom panel animation offset in pixels.
 * @param visible Panel visibility fraction from zero to one.
 */
void ui_cd_player_draw(GSGLOBAL* gs, const CdPlaybackStatus* cd, const PlayerState* player, float top_offset, float bottom_offset, float visible);
