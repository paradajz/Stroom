#pragma once

#include "audio/network/ariacast/stream.h"
#include "audio/network/ariacast/diagnostics/diagnostics.h"
#include "audio/output/device.h"

/** Failure codes for this API. */
typedef enum
{
    ARIA_PLAYBACK_ERROR_STOP = -1,
} AriaPlaybackError;

/**
 * @brief Sound state associated with one AriaCast session.
 */
typedef struct
{
    unsigned tail_blocks; /**< Successful silent lead-out writes after disconnect and the final PCM. */
    int      audible;     /**< Whether the started session owns sound output. */
    uint32_t consumed_at; /**< Listening-mode PCM consumption clock. */
    unsigned generation;  /**< Stream generation token currently associated with output. */
    int      silent;      /**< Analysis was cleared for the current underrun. */
    int      buffering;   /**< Output remains silent until the PCM reserve is rebuilt. */
    int      started;     /**< Consumption started for this session. */
} AriaPlayback;

/**
 * @brief Feed AriaCast PCM to sound and analysis, or clock-paced analysis in listening mode.
 * Transport disconnection releases short/rebuffered queues, then completes the shared sound
 * lead-out before marking the stream finished. Listening waits through the
 * final packet interval without sound writes.
 * @param playback Output state.
 * @param stream PCM queue and session.
 * @param diagnostics Optional diagnostic state; unused in release.
 * @param runtime Sound initialization/error callbacks.
 * @param now Monotonic time.
 * @param capture Called with a stereo sample-frame count after PCM consumption; zero clears analysis.
 * @param context Capture context.
 */
void aria_playback_step(AriaPlayback* playback, AriaStream* stream, AriaDiagnostics* diagnostics, const OutputRuntime* runtime, uint32_t now, void (*capture)(void* context, const uint8_t* pcm, unsigned frames), void* context);

/**
 * @brief Stop network-owned sound, retaining state for retry on failure.
 * @param playback Output state.
 * @return 0 on success, a negative AriaPlaybackError on failure.
 */
int aria_playback_stop(AriaPlayback* playback);
