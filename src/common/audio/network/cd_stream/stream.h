#pragma once
#include <stdint.h>

typedef struct
{
    unsigned clients;
    int      failed;
} CdStreamStatus;

typedef enum
{
    CD_STREAM_ERROR_CLOSE = -1,
    CD_STREAM_ERROR_OPEN  = -2
} CdStreamError;

/**
 * @brief Prepare bounded PCM capture before CD autoplay without opening networking.
 * The worker sleeps until the application enables transport.
 * @return Zero on success, a negative CdStreamError on failure; retry close after partial preparation.
 */
int cd_stream_open(void);

/**
 * @brief Poll transport lifecycle and retry failures independently of CD playback.
 * Call on the application thread.
 * @param enabled Nonzero while CD streaming should be enabled.
 * @return Current subscribed-client count and failure state.
 */
CdStreamStatus cd_stream_update(int enabled);

/**
 * @brief Copy resampled CD audio into the bounded producer mailbox.
 * CD-worker callback; performs no socket operations.
 * @param samples Borrowed interleaved PCM bytes; copied before returning.
 * @param frames Stereo frames; must equal CD_OUTPUT_FRAMES.
 */
void cd_stream_push(const uint8_t* samples, unsigned frames);

/**
 * @brief Discard queued PCM and request subscription cancellation at a discontinuity.
 * CD-worker callback; transport changes are applied by the network worker.
 */
void cd_stream_reset(void);

/**
 * @brief Mark natural playback completion so queued PCM can drain.
 * CD-worker callback; the network worker sends the end record after the mailbox drains.
 */
void cd_stream_end(void);

/**
 * @brief Record producer stalls and silence for CD stream diagnostics.
 * CD-worker callback; updates memory only and performs no socket operations.
 * @param activity Current CD_ACTIVITY_* flags.
 */
void cd_stream_activity(unsigned activity);

/**
 * @brief Join the stream worker before releasing transport and network ownership.
 * @return Zero after cleanup, positive while stopping, a negative CdStreamError when cleanup must retry.
 */
int cd_stream_close(void);
