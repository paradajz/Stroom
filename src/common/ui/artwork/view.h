#pragma once

#include "util/diagnostics.h"
#include <gsKit.h>
#include "audio/artwork/artwork.h"
#include "audio/common/artwork_observations.h"

/** Failure codes for this API. */
typedef enum
{
    UI_ARTWORK_ERROR_DECODER_START = -1,
    UI_ARTWORK_ERROR_DECODER_CLOSE = -2,
} UiArtworkError;

/** @brief Start preparation after renderer scheduling is configured; reuse existing cover VRAM. */
int ui_artwork_open(void);

/**
 * @brief Stop preparation before destroying the GS context.
 * @return 0 after cleanup, positive while stopping, a negative UiArtworkError on failure.
 * Nonzero results retain ownership for a later close attempt.
 */
int ui_artwork_close(void);

/**
 * @brief Forget the cover allocation after its graphics context has been destroyed.
 *
 * Called by frame shutdown after successful display cleanup. Decoder restarts
 * preserve this allocation; the next draw in a new context allocates and uploads.
 */
void ui_artwork_reset_texture(void);

/**
 * @brief Give a runnable decoder time before frame presentation.
 *
 * Sleeps only when decoding work is runnable. The minimum time slice may extend
 * an already expensive frame; this function does not wait for graphics or refresh.
 *
 * @param frame_started Frame start in monotonic milliseconds, used to budget the yield.
 */
void ui_artwork_yield(uint32_t frame_started);

/**
 * @brief Queue a new cover and accept completed decoding without waiting.
 * @param metadata Current track labels and URL; identity changes hide the old cover immediately.
 * @param blob Optional completed download; successful submission takes ownership and clears it.
 * @param identity Full track metadata for streaming; URL only for CD.
 */
void ui_artwork_prepare(const TrackMetadata* metadata, ArtworkBlob* blob, ArtworkIdentity identity);

/** @brief Draw cached artwork at UI-owned bounds and GS alpha (0..128); return 1 if drawn. */
int ui_artwork_draw_at(GSGLOBAL* gs, float cover_left, float cover_top, float cover_side, unsigned opacity);

#if STROOM_DIAGNOSTICS
/** @brief Set the diagnostic sink; configured before opening; decoding callbacks run on its worker. */
void ui_artwork_set_observer(ArtworkObserver observer);

/**
 * @brief Read the last main-thread artwork preparation/drawing stage.
 * @return Static diagnostic text.
 */
const char* ui_artwork_status(void);
#endif
