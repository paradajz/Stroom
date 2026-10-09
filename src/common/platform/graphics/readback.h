#pragma once

#include <gsKit.h>

/** Failure codes for this API. */
typedef enum
{
    PS2_READBACK_ERROR_CAPTURE = -1,
} Ps2ReadbackError;

/**
 * @brief Read a full-width strip of a 32-bit framebuffer into EE memory.
 *
 * Call on the renderer thread after platform_display_submit(). Completes the read
 * synchronously, synchronizes the cache and restores GS submission state.
 *
 * @param gs Initialized drawing context.
 * @param pixels 128-byte-aligned storage for DISPLAY_WIDTH * rows 32-bit pixels.
 * @param framebuffer Framebuffer address in VRAM bytes.
 * @param y First row to read.
 * @param rows Number of rows; the strip must fit the 16-bit DMA QWC limit.
 * @return 0 on success, a negative Ps2ReadbackError on failure.
 */
int platform_display_readback(GSGLOBAL* gs, void* pixels, unsigned framebuffer, unsigned y, unsigned rows);
