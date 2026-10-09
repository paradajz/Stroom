#pragma once

#include "util/diagnostics.h"
#include "audio/common/snapshot.h"
#include "audio/common/metadata.h"
#include "audio/artwork/artwork.h"
#include "audio/common/artwork_observations.h"

typedef enum
{
    NETWORK_START_FAILED           = -1,
    NETWORK_START_RESTART_REQUIRED = -2,
    NETWORK_ERROR_SERVER_START     = -3,
    NETWORK_ERROR_WORKER_START     = -4,
    NETWORK_ERROR_UNAVAILABLE      = -5,
    NETWORK_ERROR_RECEIVER         = -6,
    NETWORK_ERROR_WORKER_CLOSE     = -7,
    NETWORK_ERROR_PLATFORM_CLOSE   = -8,
    NETWORK_ERROR_MODULE_LOAD      = -9,
    NETWORK_ERROR_RPC_UNAVAILABLE  = -10,
    NETWORK_ERROR_INITIALIZE       = -11,
    NETWORK_ERROR_INTERFACE        = -12,
    NETWORK_ERROR_CONFIGURE        = -13,
    NETWORK_ERROR_CONFIG_VERIFY    = -14,
    NETWORK_ERROR_PLATFORM_CLEANUP = -15
} NetworkError;

/**
 * @brief Reset receiver state, initialize networking, and start the network audio worker.
 *
 * @return 0 when AriaCast starts, positive while previous cleanup is pending,
 * NETWORK_START_RESTART_REQUIRED for incompatible resident modules, or
 * another NetworkError for other failures.
 */
int network_open(void);

/**
 * @brief Read the network address cached by the worker.
 *
 * @return Borrowed IPv4 string, updated by network_poll.
 */
const char* network_address(void);

/** @brief Whether the session captured by network_poll requested silent listening. */
int network_listening(void);

/** @brief Borrow the sender label cached with the most recently polled/copied session. */
const char* network_device_name(void);

/**
 * @brief Read the last startup or runtime error text.
 *
 * Polling preserves startup failures until a subsequent open succeeds.
 *
 * @return Borrowed error string; empty when no failure is reported.
 */
const char* network_error(void);

/**
 * @brief Refresh activity, levels, and address without copying sample history.
 *
 * @param generation Destination stream generation token, captured with activity;
 * zero after failed startup or without a worker lock. Runtime failures retain
 * the last published generation.
 * @return 1 while PCM is active, 0 while idle, or NETWORK_ERROR_UNAVAILABLE / NETWORK_ERROR_RECEIVER.
 */
int network_poll(unsigned* generation);

/**
 * @brief Copy activity, generation, audio and labels together under the worker lock.
 *
 * @param generation Destination current stream generation, which may differ from the preceding poll.
 * @param audio Destination audio; cleared when inactive or unavailable.
 * @param metadata Destination labels; cleared when inactive or unavailable.
 * @return 1 while active, 0 while idle, or NETWORK_ERROR_UNAVAILABLE / NETWORK_ERROR_RECEIVER; generation is zero after failed startup or without a lock.
 */
int network_copy_snapshot(unsigned* generation, Audio* audio, TrackMetadata* metadata);

#if STROOM_DIAGNOSTICS
/**
 * @brief Queue a main-thread timing observation; stale identities are ignored.
 * @param metadata Track identity for the observation.
 * @param observation Completed phase and timing information.
 */
void network_artwork_observe(const TrackMetadata* metadata, ArtworkObservation observation);

/**
 * @brief Report the main-thread cover stage for UDP diagnostics without touching sockets.
 * @param metadata Displayed track labels and URL; stale reports are ignored.
 * @param stage Static ASCII description without JSON quotes or escapes.
 */
void network_artwork_display(const TrackMetadata* metadata, const char* stage);
#endif

/**
 * @brief Transfer artwork only when it matches the requested metadata snapshot.
 * @param metadata Track labels and URL currently held by the caller.
 * @param blob Destination; unchanged on mismatch, caller frees data after transfer.
 * @return 0 on transfer, positive when no matching result is available.
 */
int network_take_artwork(const TrackMetadata* metadata, ArtworkBlob* blob);

/**
 * @brief Stop the network worker and release its sockets and synchronization resources.
 * @return 0 after cleanup, positive while stopping, a negative NetworkError on failure.
 * Nonzero results retain resources for a later close retry.
 */
int network_close(void);
