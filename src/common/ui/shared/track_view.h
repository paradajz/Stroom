#pragma once

#include <gsKit.h>
#include <stdint.h>
#include "audio/common/metadata.h"

/** Draw available track labels and artwork in the lower player panel. */
void ui_track_details_draw(GSGLOBAL* gs, const TrackMetadata* metadata, float bottom_offset, uint32_t now_ms);

/** Fit a static label to its available character count. */
void ui_track_text_fit(char* out, const char* text, unsigned limit);
