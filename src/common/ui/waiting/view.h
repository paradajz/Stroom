#pragma once

#include "audio/source/source.h"
#include "ui/screen.h"
#include <gsKit.h>

/** Draw source availability and connection instructions. */
void ui_waiting_draw(GSGLOBAL* gs, const AudioSourceStatus* source, UiScreen screen, float bottom_offset);
