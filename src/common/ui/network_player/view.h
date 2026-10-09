#pragma once

#include <gsKit.h>

/**
 * @brief Draw an optional centered sender label without track widgets.
 * @param gs Graphics context.
 * @param device_name Sender label; empty text hides the label.
 * @param bottom_offset Vertical panel animation offset in pixels.
 */
void ui_network_listening_draw(GSGLOBAL* gs, const char* device_name, float bottom_offset);
