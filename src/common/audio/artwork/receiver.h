#pragma once

#include "audio/artwork/artwork.h"
#include "audio/artwork/http.h"
#include "audio/common/artwork_observations.h"
#include "util/diagnostics.h"

/**
 * @brief Worker-owned incremental HTTP fetch using nonblocking sockets.
 */
typedef struct
{
    int         fd;           /**< Socket or minus one after initialization. */
    int         initialized;  /**< Nonzero after first service call. */
    unsigned    attempts;     /**< Fetch attempts for the current artwork identity. */
    uint32_t    metadata_at;  /**< Last artwork identity change; fetch waits for settling. */
    uint32_t    started;      /**< Start of current attempt. */
    uint32_t    progress_at;  /**< Last successful request write or response read. */
    uint32_t    polled;       /**< Last socket service timestamp. */
    uint32_t    retry_at;     /**< Time of the preceding failure. */
    ArtworkHttp http;         /**< Response framing state. */
    unsigned    capacity;     /**< Allocated compressed buffer bytes; grows only as data arrives. */
    ArtworkBlob blob;         /**< Pending body or completed result. */
    char        request[512]; /**< HTTP GET request. */
    unsigned    request_size; /**< Request byte count. */
    unsigned    sent;         /**< Request bytes already written. */
    int         complete;     /**< Terminal state for this identity: downloaded (possibly consumed), unsupported URL, or oversized response. */

#if STROOM_DIAGNOSTICS
    ArtworkObserver observer;    /**< Worker-thread timing sink, preserved by close/reset. */
    uint32_t        max_step_ms; /**< Longest service call, measured by the worker for this track. */
    unsigned        received;    /**< Raw response bytes read, including headers and chunk framing. */
    const char*     stage;       /**< Current operation, retained when an attempt fails. */
    const char*     failure;     /**< Failure reason for this attempt, or null; static ASCII. */
    int             error;       /**< Last socket error code, zero for HTTP/framing failures. */
#endif
} ArtworkReceiver;

/**
 * @brief Advance an image fetch; changing artwork identity cancels the old request.
 *
 * Transfer steps issue at most one send or read_limit receives of up to 4 KiB.
 * Reads stop after a short read, completion, error, or 4 ms elapsed between reads.
 * A single-read caller does not sample the clock for this burst limit. Setup,
 * error queries and cleanup can issue additional socket calls. PS2 socket RPCs
 * are synchronous; step duration is unbounded, and deadlines are checked only
 * between calls. New fetches wait for 250 ms of stable artwork identity.
 * An unchanged fetch gates service attempts at 10 ms intervals.
 *
 * @param receiver Zero-initialized worker state.
 * @param metadata Current track and artwork URL, or NULL to cancel and discard.
 * @param identity Full track metadata for Aria; URL only for CD.
 * @param now Monotonic milliseconds.
 * @param nonblocking Platform socket configuration callback; returns 0 on success, negative on failure.
 * @param read_limit Maximum receives per step; pass 1 for network audio, 4 for CD.
 */
void artwork_receiver_step(ArtworkReceiver* receiver, const TrackMetadata* metadata, ArtworkIdentity identity, uint32_t now, int (*nonblocking)(int), unsigned read_limit);

/**
 * @brief Transfer a completed compressed image to the caller.
 * @param receiver Worker state.
 * @param result Receives ownership on success.
 * @return 0 on transfer, positive when no matching result is available.
 */
int artwork_receiver_take(ArtworkReceiver* receiver, ArtworkBlob* result);

/**
 * @brief Cancel a fetch and release its socket and compressed storage.
 * @param receiver Worker state.
 */
void artwork_receiver_close(ArtworkReceiver* receiver);
