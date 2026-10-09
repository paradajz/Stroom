#pragma once

#include "platform/cd/drive.h"

/** Playback-owned snapshot; hardware and DMA remain on the reader worker. */
typedef struct
{
    Ps2CdDrive state; /**< Most recently collected drive state. */
} Ps2CdReader;

/**
 * @brief Initialize the process-wide CDVD client and start its reader worker.
 * Startup initializes services before audio starts. Subsequent device operations,
 * including media probes, run on the worker without holding its mailbox lock.
 * @param reader Playback-owned snapshot to initialize.
 * @param runtime Startup progress, lifetime flag and diagnostic storage.
 * @return Zero on success, a negative Ps2CdDriveError on failure. Close the reader
 * after a failed worker start before attempting to open it again.
 */
int platform_cd_reader_open(Ps2CdReader* reader, const Ps2CdDriveRuntime* runtime);

/**
 * @brief Collect the latest media observation and acknowledge its change flag.
 * @param destination Receives the cached observation, including any unacknowledged change.
 * @return Zero when a media observation is available, positive before the first probe.
 */
int platform_cd_reader_media(Ps2CdMedia* destination);

/**
 * @brief Queue a track-table read or collect a completed request.
 * @param toc Receives the raw table and SDK error when the result is ready.
 * Borrowed bytes remain valid until the next completed TOC request.
 * @return Positive while pending, zero on success, a negative Ps2CdDriveError on failure.
 */
int platform_cd_reader_toc(Ps2CdToc* toc);

/**
 * @brief Check whether the collected snapshot has no read or spindle command pending.
 * @param reader Snapshot refreshed by platform_cd_reader_poll().
 * @return Nonzero when idle and not failed; zero otherwise.
 */
int platform_cd_reader_idle(const Ps2CdReader* reader);

/**
 * @brief Check whether the collected snapshot requires a console restart.
 * @param reader Snapshot refreshed by platform_cd_reader_poll().
 * @return Nonzero after an unresponsive drive has been disabled.
 */
int platform_cd_reader_failed(const Ps2CdReader* reader);

/**
 * @brief Invalidate ready sectors and queue cancellation and a new read position.
 * A reset supersedes any queued motor stop. Device work remains on the worker.
 * @param reader Playback snapshot whose buffered and completion counts are cleared.
 * @param start Next absolute sector to read when prefetch is requested.
 * @return Zero when queued, PS2_CD_DRIVE_ERROR_RESTART_REQUIRED if the drive failed.
 */
int platform_cd_reader_reset(Ps2CdReader* reader, int start);

/**
 * @brief Collect drive progress and consume the pending error notification.
 * @param reader Snapshot to refresh; a queued reset delays publication of its new state.
 * @param now Caller timestamp, unused because the reader worker samples its own clock.
 * @return Zero without a new error, a negative Ps2CdDriveError on failure.
 * A restart-required failure remains latched across calls.
 */
int platform_cd_reader_poll(Ps2CdReader* reader, uint32_t now);

/**
 * @brief Borrow the next completed sector without advancing the reserve.
 * @param reader Reader handle; the reserve belongs to the process-wide worker.
 * @return Sector bytes, or NULL when empty or failed. The pointer is invalidated
 * by consumption, reset, a media change or reader shutdown.
 */
const uint8_t* platform_cd_reader_sector(Ps2CdReader* reader);

/**
 * @brief Count completed sectors currently available to playback.
 * @param reader Reader handle.
 * @return Available sector count, or zero after a latched drive failure.
 */
unsigned platform_cd_reader_buffered(const Ps2CdReader* reader);

/**
 * @brief Release the next completed sector so the worker can reuse its storage.
 * @param reader Reader handle; an empty reserve is left unchanged.
 */
void platform_cd_reader_consume(Ps2CdReader* reader);

/**
 * @brief Enable read-ahead up to an exclusive sector boundary.
 * A fresh request supersedes a queued motor stop. An accepted stop settles before
 * standby restores the spindle and reads resume.
 * @param reader Reader handle.
 * @param end Exclusive absolute sector boundary.
 * @return Zero when requested, PS2_CD_DRIVE_ERROR_RESTART_REQUIRED if the drive failed.
 */
int platform_cd_reader_prefetch(Ps2CdReader* reader, int end);

/**
 * @brief Check whether playback has no usable completed sector.
 * @param reader Reader handle.
 * @return Nonzero when the reserve is empty or the drive failed.
 */
int platform_cd_reader_waiting(const Ps2CdReader* reader);

/**
 * @brief Disable further read-ahead after an output or recovery error.
 * Completed sectors and an outstanding command remain owned by the reader.
 * @param reader Reader handle.
 */
void platform_cd_reader_suspend(Ps2CdReader* reader);

/**
 * @brief Disable read-ahead and queue an audio-CD motor stop on the worker.
 * Pending reads settle before the motor command. A reset or fresh prefetch
 * supersedes a queued stop; an accepted stop settles before reads resume.
 * @param reader Reader handle.
 */
void platform_cd_reader_stop(Ps2CdReader* reader);

/**
 * @brief Request reader shutdown and join it without blocking playback.
 * Retain the adapter until cleanup succeeds, including after startup failure.
 * @param reader Receives the final drive snapshot after successful cleanup.
 * @return Zero after cleanup, positive while the worker exits, negative when
 * resource cleanup failed and must be retried.
 */
int platform_cd_reader_close(Ps2CdReader* reader);
