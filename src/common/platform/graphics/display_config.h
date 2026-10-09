#pragma once

#include "contracts/display.h"

/* Selected output profile. Keep hardware selection, framebuffer geometry and
 * timing together. GS tokens are expanded only by the platform adapter; portable
 * consumers need no SDK headers to use the dimensions and timing below. */
#if DISPLAY_INTERLACED
#define DISPLAY_GS_MODE    GS_MODE_NTSC
#define DISPLAY_INTERLACE  GS_INTERLACED
#define DISPLAY_FIELD_MODE GS_FIELD
#else
#define DISPLAY_GS_MODE    GS_MODE_DTV_480P
#define DISPLAY_INTERLACE  GS_NONINTERLACED
#define DISPLAY_FIELD_MODE GS_FRAME
#endif

#define DISPLAY_REFRESH_HZ ((double)DISPLAY_REFRESH_NUMERATOR / DISPLAY_REFRESH_DENOMINATOR)
#define DISPLAY_FRAME_US   (1000000ull * DISPLAY_REFRESH_DENOMINATOR / DISPLAY_REFRESH_NUMERATOR)
