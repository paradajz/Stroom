#pragma once

#include <gsKit.h>
#include <stdint.h>
#include "audio/common/metadata.h"

/**
 * @brief Draw available track labels and artwork in the lower player panel.
 * @param gs Graphics context.
 * @param metadata Current track metadata.
 * @param bottom_offset Vertical panel animation offset in pixels.
 * @param now_ms Wrapping monotonic milliseconds for scrolling labels.
 */
void ui_track_details_draw(GSGLOBAL* gs, const TrackMetadata* metadata, float bottom_offset, uint32_t now_ms);

/**
 * @brief Fit a static UTF-8 label to its available display-character count.
 * @param out Destination with UI_PLAYER_LABEL_BYTES capacity.
 * @param text Null-terminated source label.
 * @param limit Maximum display-character count, including any truncation marker.
 */
void ui_track_text_fit(char* out, const char* text, unsigned limit);
