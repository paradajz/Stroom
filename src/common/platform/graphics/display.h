#pragma once

#include <gsKit.h>

/** Failure codes for this API. */
typedef enum
{
    PS2_DISPLAY_ERROR_INVALID_CONTEXT  = -1,
    PS2_DISPLAY_ERROR_SEMAPHORE_DELETE = -2,
} Ps2DisplayError;

/**
 * @brief Initialize the configured display, GIF DMA and vertical-blank notification.
 *
 * Call once on the renderer thread. The graphics context and refresh resources
 * remain allocated until platform_display_close() succeeds. Reopening is
 * rejected while a context or failed-start cleanup remains allocated.
 *
 * @return Graphics context, or NULL if context allocation, semaphore creation,
 * or vertical-blank handler registration fails. Call close with NULL after a
 * failed open to retry any retained cleanup.
 */
GSGLOBAL* platform_display_open(void);

/**
 * @brief Submit the drawing queue and wait for this frame's GS work to finish.
 * @param gs Initialized drawing context, used on the renderer thread.
 */
void platform_display_submit(GSGLOBAL* gs);

/**
 * @brief Remove vertical-blank notification and release the display context.
 * @param gs Context returned by successful platform_display_open(), or NULL to
 * clean up failed startup. No pending GS work.
 * @return 0 on success, a negative Ps2DisplayError on failure.
 * Call on the renderer thread after artwork and drawing stop.
 */
int platform_display_close(GSGLOBAL* gs);

/**
 * @brief Wait for an eligible refresh and present the completed drawing buffer.
 *
 * Call on the renderer thread after submitting commands and waiting for graphics
 * completion with platform_display_submit(). This function waits for vertical blank
 * using a semaphore; it does not submit commands or wait for GS completion.
 * It flips double buffers and selects the next drawing buffer. While gsKit's
 * FirstFrame flag is set, it only selects the drawing buffer without waiting.
 *
 * @param gs Context returned by a successful platform_display_open().
 * @param refresh_interval Minimum refreshes between presentations, chosen by
 * the caller. Zero uses one refresh. A late frame still waits for a future refresh.
 */
void platform_display_present(GSGLOBAL* gs, unsigned refresh_interval);
