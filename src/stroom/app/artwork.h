#pragma once

#include "audio/source/source.h"

/** Failure codes for this API. */
typedef enum
{
    APP_ARTWORK_ERROR_START = -1,
    APP_ARTWORK_ERROR_CLOSE = -2,
} AppArtworkError;

/**
 * @brief Wire artwork diagnostics and start background decoding.
 *
 * Call on the renderer thread after configuring its scheduling and before
 * preparing frames. Reopening first retries close; failed cleanup leaves the
 * observer attached and blocks reopening. Diagnostic builds connect the
 * decoder and upload observer to the network diagnostic sink.
 *
 * @return 0 when the decoder starts, positive while cleanup is pending, negative when unavailable.
 * Decoder failure does not prevent playback or subsequent frame preparation.
 */
int app_artwork_open(void);

/**
 * @brief Stop background decoding and disconnect artwork diagnostics.
 *
 * Call on the renderer thread before destroying the graphics context, without
 * concurrent adapter calls. Active decoding leaves cleanup pending until the
 * worker exits and releases its owned buffers.
 * @return 0 after cleanup, positive while stopping, a negative AppArtworkError on failure.
 * Nonzero results retain ownership for a later close attempt.
 */
int app_artwork_close(void);

/**
 * @brief Hand the current source's cover to the UI, including during fullscreen playback.
 *
 * Preserves URL identity for CDs and full track identity for network audio.
 * Clears the cover while detecting, waiting, or listening. Frees downloaded
 * buffers that the decoder does not accept. Call once on the renderer thread
 * before drawing each frame, after polling sources and CD recognition.
 *
 * @param source Non-NULL current source snapshot; borrowed for this call and
 * left unchanged.
 */
void app_artwork_prepare(const AudioSourceStatus* source);
