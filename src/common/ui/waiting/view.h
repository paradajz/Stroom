#pragma once

#include "audio/source/source.h"
#include "ui/screen.h"
#include <gsKit.h>

/**
 * @brief Draw source availability and connection instructions.
 * @param gs Graphics context.
 * @param source Current source snapshot.
 * @param screen Current initializing, detecting or waiting screen.
 * @param bottom_offset Vertical panel animation offset in pixels.
 */
void ui_waiting_draw(GSGLOBAL* gs, const AudioSourceStatus* source, UiScreen screen, float bottom_offset);
