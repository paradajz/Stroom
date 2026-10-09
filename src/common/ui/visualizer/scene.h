#pragma once

#include <gsKit.h>
#include "milkdrop/music.h"
#include "milkdrop/director.h"

#if STROOM_PRESET_BENCHMARK
/** @brief Raw bus-clock timings for the most recent scene draw, including preemption. */
typedef struct
{
    uint64_t mesh_ticks;
    uint64_t emit_ticks;
    uint64_t command_ticks;
} SceneTiming;

/** @brief Read benchmark-only timings; emit includes command time. */
SceneTiming scene_timing(void);
#endif

/**
 * @brief Render the current presets and maintain feedback history.
 *
 * @param gs GS drawing context.
 * @param director Director; rendering may update per-vertex state.
 * @param s Current audio features.
 * @param use_feedback Nonzero to enable feedback.
 * @param preserve_overlays Nonzero when overlays require clean feedback history.
 */
void scene_draw(GSGLOBAL* gs, Director* director, const MusicFeatures* s, int use_feedback, int preserve_overlays);

/**
 * @brief Invalidate displayed and raw feedback history.
 */
void scene_reset(void);

/**
 * @brief Release scene caches after graphics work completes and its context is destroyed.
 *
 * Invalidates feedback history and VRAM addresses, frees color-curve CPU packets,
 * and clears allocation failures so the next context can prepare its resources.
 * Called by successful frame shutdown; safe to repeat.
 */
void scene_close(void);
