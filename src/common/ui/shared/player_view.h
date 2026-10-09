#pragma once

#include "ui/shared/style.h"
#include "audio/source/source.h"

/**
 * @brief Draw shared stereo meters at their fixed screen positions.
 * @param gs Graphics context.
 */
void ui_player_meters_draw(GSGLOBAL* gs);

/**
 * @brief Draw source and playback status in the upper panel.
 * @param gs Graphics context.
 * @param source Current source snapshot.
 * @param waiting Nonzero while detecting or waiting for a source.
 * @param muted Nonzero when sound is muted.
 * @param offset Vertical panel animation offset in pixels.
 */
void ui_player_source_draw(GSGLOBAL* gs, const AudioSourceStatus* source, int waiting, int muted, float offset);
