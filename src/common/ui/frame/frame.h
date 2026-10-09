#pragma once

#include "ui/screen.h"
#include "ui/cd_player/controller.h"
#include "ui/settings/controller.h"
#include "milkdrop/runtime.h"
#include "audio/source/source.h"

/** Failure codes for this API. */
typedef enum
{
    UI_FRAME_ERROR_ALREADY_OPEN  = -1,
    UI_FRAME_ERROR_DISPLAY_OPEN  = -2,
    UI_FRAME_ERROR_DISPLAY_CLOSE = -3,
} UiFrameError;

/**
 * @brief Open the renderer and configured display on the renderer thread.
 * @return 0 on success, a negative UiFrameError on failure.
 * After failure, call ui_frame_close() before retrying startup.
 */
int ui_frame_open(void);

/**
 * @brief Release the renderer display after drawing and artwork workers stop.
 * @return 0 after cleanup, positive while stopping, a negative UiFrameError on failure.
 * Nonzero results retain ownership for a later close attempt.
 * Also call after failed startup to finish any retained display cleanup.
 */
int ui_frame_close(void);

/**
 * @brief Compose and present the selected screen and overlays.
 *
 * Call after ui_frame_open(), once per frame on the renderer thread. Submits the
 * graphics queue, waits for completion, yields to artwork decoding and presents through the display
 * adapter using the frame rate in settings.
 *
 * @param screen Screen selected by the application controller.
 * @param player Read-only player navigation.
 * @param settings Read-only menu, overlay and frame-rate options.
 * @param visualizer MilkDrop state; rendering may update preset caches.
 * @param audio Current audio snapshot.
 * @param source Current source snapshot.
 * @param now_ms Frame start in wrapping monotonic milliseconds, used for UI
 * animation and the decoder yield budget.
 */
void ui_frame_render(UiScreen screen, const PlayerState* player, const AppSettings* settings, MilkdropRuntime* visualizer, const Audio* audio, const AudioSourceStatus* source, uint32_t now_ms);

#if STROOM_PRESET_BENCHMARK
/** @brief Bus-clock timestamps for the latest completed benchmark frame. */
typedef struct
{
    uint64_t drawn_ticks;     /**< Composition completed, before graphics submission. */
    uint64_t submitted_ticks; /**< Graphics submission and completion wait finished. */
    uint64_t presented_ticks; /**< Decoder yield and display presentation finished. */
} UiFrameTiming;

/**
 * @brief Read the latest benchmark frame's phase boundaries.
 *
 * Call on the renderer thread after ui_frame_render(). Timestamps use the PS2
 * bus clock, include preemption and waits, and are replaced by the next render.
 *
 * @return Copied timestamps; all zero before the first completed frame.
 */
UiFrameTiming ui_frame_timing(void);
#endif
