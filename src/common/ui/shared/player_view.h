#pragma once

#include "ui/shared/style.h"
#include "audio/source/source.h"

/** Draw shared stereo meters at their fixed screen positions. */
void ui_player_meters_draw(GSGLOBAL* gs);

/** Draw the current source and playback status. */
void ui_player_source_draw(GSGLOBAL* gs, const AudioSourceStatus* source, int waiting, int muted, float offset);
