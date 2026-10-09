#pragma once

#include "audio/source/source.h"
#include "audio/artwork/artwork.h"

/** Failure codes for this API. */
typedef enum
{
    CD_LOOKUP_ERROR_WORKER_CLOSE  = -1,
    CD_LOOKUP_ERROR_NETWORK_CLOSE = -2,
} CdLookupError;

/**
 * @brief Start optional CD recognition independently of playback.
 *
 * Call from the main thread before polling; close before opening again. An empty
 * host disables recognition. Invalid addresses or startup failures leave the
 * client inactive and do not prevent CD playback. Networking setup is synchronous;
 * lookup and artwork transfers run on the worker after startup.
 * Opening while previous resources remain allocated leaves the client unchanged.
 *
 * @param host Non-NULL, NUL-terminated numeric IPv4 address; copied during this call.
 */
void cd_lookup_open(const char* host);

/**
 * @brief Publish available labels and schedule requests for the current disc/track.
 *
 * Call on the main thread. Performs no socket I/O and leaves source unchanged
 * when inactive. A non-CD or absent-disc snapshot clears pending client metadata
 * and artwork; a new disc or track schedules a correlated request.
 *
 * @param source Current source snapshot; its metadata is updated when applicable.
 */
void cd_lookup_poll(AudioSourceStatus* source);

/**
 * @brief Take a downloaded CD cover whose URL matches the supplied metadata.
 *
 * Call on the main thread. Success removes the pending image from the client and
 * copies the supplied labels into the returned blob. The caller owns blob->data
 * and must free it or transfer it to a consumer that accepts ownership.
 *
 * @param metadata Non-NULL current metadata used for URL matching.
 * @param blob Non-NULL destination with no owned buffer to overwrite.
 * @return 0 on transfer, positive when no matching result is available.
 *         leaving blob unchanged.
 */
int cd_lookup_take_artwork(const TrackMetadata* metadata, ArtworkBlob* blob);

/**
 * @brief Stop the lookup worker and release client-owned resources.
 *
 * Call on the main thread, without concurrent client API calls. Returns pending
 * while the worker is active, including during a synchronous SDK operation.
 * Releases sockets, synchronization objects, unclaimed images and this client's
 * networking reference. Previously transferred blobs remain owned by their callers.
 * Safe when inactive or already closed; the client may be opened again after success.
 * @return 0 after cleanup, positive while stopping, a negative CdLookupError on failure.
 * Nonzero results retain ownership for a later close attempt.
 */
int cd_lookup_close(void);
