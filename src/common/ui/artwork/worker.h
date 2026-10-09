#pragma once

#include "audio/artwork/artwork.h"
#include "audio/common/artwork_observations.h"
#include "ui/artwork/decode.h"

/** Failure codes for this API. */
typedef enum
{
    ARTWORK_WORKER_ERROR_ALREADY_OPEN = -1,
    ARTWORK_WORKER_ERROR_PRIORITY     = -2,
    ARTWORK_WORKER_ERROR_START        = -3,
    ARTWORK_WORKER_ERROR_CLOSE        = -4,
    ARTWORK_WORKER_ERROR_INVALID_BLOB = -5,
    ARTWORK_WORKER_ERROR_UNAVAILABLE  = -6,
} ArtworkWorkerError;

/* Open, submit, take and close are owned by the render thread. The observer
 * must be configured before opening and left unchanged until close succeeds. */

/**
 * @brief Start a decoder below the calling renderer's priority; no synchronous fallback.
 * @return 0 on success, a negative ArtworkWorkerError on failure.
 */
int artwork_worker_open(void);

/**
 * @brief Request decoder shutdown and attempt cleanup without waiting for thread exit.
 * @return 0 after cleanup, positive while stopping, a negative ArtworkWorkerError on failure.
 * Nonzero results retain ownership for a later close attempt.
 */
int artwork_worker_close(void);

/**
 * @brief Yield renderer time to runnable artwork decoding.
 *
 * Called after graphics submission completes and before frame presentation. Sleeps only while
 * decoding work is runnable, using ui_background_work_budget(): remaining frame
 * time after the presentation reserve, with a minimum 4 ms sleep even when the
 * frame budget is exhausted. This can extend an already expensive frame.
 *
 * @param frame_started Frame start in monotonic milliseconds, used to calculate elapsed time.
 */
void artwork_worker_yield(uint32_t frame_started);

/**
 * @brief Replace the pending cover, taking ownership and clearing blob on success.
 * @return 0 on transfer, negative when the worker or blob is unavailable.
 */
int artwork_worker_submit(ArtworkBlob* blob);

/**
 * @brief Collect a matching decode completion without waiting; discard stale results.
 *
 * A collected completion is consumed even when decoding failed. Its image may
 * contain partial or previous pixels and must only be displayed when valid is
 * nonzero. Stale completions are consumed without changing either destination.
 *
 * @param metadata Current metadata used to match the completion.
 * @param image Destination copied for a matching completion; no buffer ownership transfer.
 * @param valid Decode success destination; populated only for a matching completion.
 * @param identity Full metadata matching for Aria or URL matching for CD.
 * @return 0 for a matching completion, including decode failure; positive when unavailable
 * or stale, leaving image and valid unchanged.
 */
int artwork_worker_take(const TrackMetadata* metadata, ArtworkImage* image, int* valid, ArtworkIdentity identity);
#if STROOM_DIAGNOSTICS
/** @brief Configure the observer before opening; callbacks run on the decoder thread. */
void artwork_worker_set_observer(ArtworkObserver sink);
#endif
