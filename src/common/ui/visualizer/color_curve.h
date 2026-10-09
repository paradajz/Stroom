#pragma once

#include <gsKit.h>

/** Failure codes for this API. */
typedef enum
{
    SCENE_COLOR_CURVE_ERROR_PREPARE = -1,
} SceneColorCurveError;

#define SCENE_COLOR_CURVE_SCALE 2

/**
 * @brief Queue half-resolution classic brighten/darken/solarize colour curves of the active drawing buffer.
 *
 * Call on the renderer thread with the scene batch flushed. Feedback history is
 * not modified. Success queues work; the caller still submits and waits for it.
 * The pass leaves the drawing buffer, full-display scissor and context offsets
 * selected, but changes texture/clamp/primitive state and disables PrimAlphaEnable.
 * Subsequent drawing must establish its required state.
 *
 * Scratch VRAM and DMA packets are retained internally for the graphics context's
 * lifetime. Allocation failure is latched until scene_color_curve_close(). Close
 * before using a replacement context or resetting its VRAM allocator.
 *
 * @param gs Initialized graphics context using the configured display geometry.
 * @param brighten Nonzero for invert-square-invert brightening.
 * @param darken Nonzero to square RGB after brightening; all enabled effects share one pass.
 * @param solarize Nonzero to apply classic inverse modulation and doubling after darken.
 * @return 0 on success, a negative SceneColorCurveError on failure.
 */
int scene_color_curve(GSGLOBAL* gs, int brighten, int darken, int solarize);

/** @brief Release CPU packets and forget VRAM after graphics work completes and its context closes. */
void scene_color_curve_close(void);
