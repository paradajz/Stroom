#pragma once

#include "audio/common/snapshot.h"
#include "util/diagnostics.h"
#include "audio/cd/cd_status.h"

/** Failure codes for this API. */
typedef enum
{
    CD_ERROR_ALREADY_OPEN       = -1,
    CD_ERROR_WORKER_START       = -2,
    CD_ERROR_WORKER_UNAVAILABLE = -3,
    CD_ERROR_WORKER_CLOSE       = -4,
    CD_ERROR_RESTART_REQUIRED   = -5,
} CdError;

/**
 * @brief Start the CD worker; drive and sound initialization run asynchronously.
 *
 * @param autoplay Start newly inserted discs automatically when nonzero.
 * @return 0 on success, a negative CdError on failure.
 */
int cd_open(int autoplay);

/**
 * @brief Refresh PCM levels and copy disc status without copying sample history.
 *
 * Failed worker startup reports an error even while rollback resources remain allocated.
 * @param status Destination disc status.
 * @return 0 when the worker snapshot was copied, negative when unavailable or exited.
 * CD_ERROR_RESTART_REQUIRED means startup cannot be retried until console restart.
 * Other failures retain their details; close before retrying startup.
 */
int cd_poll(CdPlaybackStatus* status);

/**
 * @brief Copy current disc status and audio together under the worker lock.
 *
 * @param status Destination status; may reflect a disc change after the preceding poll.
 * @param audio Destination audio; cleared when no disc is present or the worker is unavailable.
 */
void cd_copy_snapshot(CdPlaybackStatus* status, Audio* audio);

/**
 * @brief Stop the worker and release CD playback resources.
 * @return 0 after cleanup, positive while stopping, a negative CdError on failure.
 * Nonzero results retain resources for a later close retry.
 */
int cd_close(void);

/**
 * @brief Copy exact track boundaries for the requested, ready disc generation.
 * @return 0 when copied, positive when the requested disc is unavailable.
 */
int cd_copy_toc(unsigned generation, CdToc* toc);
