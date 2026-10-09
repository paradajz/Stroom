#include "platform/graphics/readback.h"
#include "platform/graphics/display_config.h"
#include "platform/memory/cache.h"
#include <screenshot.h>

int platform_display_readback(GSGLOBAL* gs, void* pixels, unsigned framebuffer, unsigned y, unsigned rows)
{
    /* The SDK uses width as framebuffer pitch and addresses VRAM in 256-byte units. */
    int success = ps2_screenshot(pixels, framebuffer / 256, 0, y, DISPLAY_WIDTH, rows, GS_PSM_CT32);

    platform_cache_writeback();

    /* Readback consumes FINISH; the next queue must not wait for that cleared event. */
    gs->FirstFrame = GS_SETTING_ON;

    return success ? 0 : PS2_READBACK_ERROR_CAPTURE;
}
