#pragma once

#include "audio/cd/cd_pcm.h"
#include "audio/cd/cd_runtime.h"
#include "audio/output/device.h"

/** Failure codes for this API. */
typedef enum
{
    CD_OUTPUT_ERROR_VOLUME           = OUTPUT_ERROR_VOLUME,
    CD_OUTPUT_ERROR_DRIVER_LOAD      = OUTPUT_ERROR_DRIVER_LOAD,
    CD_OUTPUT_ERROR_SERVICE_LOAD     = OUTPUT_ERROR_SERVICE_LOAD,
    CD_OUTPUT_ERROR_INITIALIZE       = OUTPUT_ERROR_INITIALIZE,
    CD_OUTPUT_ERROR_FORMAT           = OUTPUT_ERROR_FORMAT,
    CD_OUTPUT_ERROR_RPC_UNAVAILABLE  = OUTPUT_ERROR_RPC_UNAVAILABLE,
    CD_OUTPUT_ERROR_PREPARE          = OUTPUT_ERROR_PREPARE,
    CD_OUTPUT_ERROR_CLEANUP_PENDING  = OUTPUT_ERROR_CLEANUP_PENDING,
    CD_OUTPUT_ERROR_LOCK_CREATE      = OUTPUT_ERROR_LOCK_CREATE,
    CD_OUTPUT_ERROR_LOCK_UNAVAILABLE = OUTPUT_ERROR_LOCK_UNAVAILABLE,
    CD_OUTPUT_ERROR_INVALID_WRITE    = OUTPUT_ERROR_INVALID_WRITE,
    CD_OUTPUT_ERROR_QUERY            = OUTPUT_ERROR_QUERY,
    CD_OUTPUT_ERROR_WRITE            = OUTPUT_ERROR_WRITE,
    CD_OUTPUT_ERROR_STOP             = OUTPUT_ERROR_STOP,
    CD_OUTPUT_ERROR_QUIT             = OUTPUT_ERROR_QUIT,
    CD_OUTPUT_ERROR_LOCK_DELETE      = OUTPUT_ERROR_LOCK_DELETE,
    CD_OUTPUT_ERROR_CANCELLED        = OUTPUT_ERROR_CANCELLED,
} CdOutputError;

/* Sixteen submissions cover more than the 20 KiB audsrv ring, even with 2 KiB silence blocks. */
#define CD_OUTPUT_BLOCKS 16

/**
 * @brief One successful sound submission, used to distinguish music from silence.
 */
typedef struct
{
    int bytes; /**< Submitted output bytes. */
    int audio; /**< Nonzero for CD samples; zero for inserted silence. */
} CdOutputBlock;

/**
 * @brief Worker-owned sound readiness, submission history, and resampling state.
 */
typedef struct
{
    int           ready;                    /**< Nonzero after audsrv format and volume setup succeeds. */
    CdOutputBlock blocks[CD_OUTPUT_BLOCKS]; /**< Recent submissions in playback order. */
    unsigned      block_write;              /**< Next submission history slot. */
    unsigned      block_count;              /**< Number of valid history entries. */
    CdResampler   resampler;                /**< Sample interpolation carried between sectors. */
    CdResampler   pending_resampler;        /**< Interpolation history committed only after the pending PCM is accepted. */
    int           pending;                  /**< Nonzero while converted PCM awaits submission. */
} CdOutput;

/**
 * @brief Initialize sound output, reusing it if already ready.
 * @param o Output state.
 * @param runtime Worker lifetime and progress callbacks.
 * @param status Progress/error destination.
 * @return 0 on success, a negative CdOutputError on failure.
 */
int cd_output_open(CdOutput* o, const CdRuntime* runtime, CdPlaybackStatus* status);

/**
 * @brief Stop sound only while CD owns the shared device.
 * @param o Output state.
 * @return 0 on success, a negative CdOutputError on failure.
 */
int cd_output_stop(const CdOutput* o);

/**
 * @brief Discard pending PCM, reset resampling and optionally flush audsrv before playback resumes.
 * @param o Output state.
 * @param runtime Worker lifetime callbacks.
 * @param resume Nonzero to prepare sound for immediate playback.
 * @return 0 on success, positive when unselected, negative on preparation failure.
 */
int cd_output_reset(CdOutput* o, const CdRuntime* runtime, int resume);

/**
 * @brief Convert once and retain the sector across busy/error retries until output accepts it.
 * @param o Output state.
 * @param sector Raw CD sector bytes; retry the same sector until accepted or reset.
 * @param samples Receives borrowed submitted PCM bytes on success, valid until next submission.
 * @return 0 when submitted, positive when busy or unselected, a negative CdOutputError on failure.
 */
int cd_output_submit(CdOutput* o, const uint8_t* sector, const uint8_t** samples);

/**
 * @brief Read total queued bytes and the CD audio portion of that queue.
 * @param o Output submission history.
 * @param audio_bytes Receives queued CD bytes, excluding silence; zero on error.
 * @return Queued bytes, or a negative CdOutputError.
 */
int cd_output_queued(const CdOutput* o, int* audio_bytes);

/**
 * @brief Feed silence during underrun or lead-out to prevent stale audsrv replay.
 * @param o Output submission history.
 * @param queued Previously sampled queue depth.
 * @return 0 when submitted, positive when busy or unselected, a negative CdOutputError on failure.
 */
int cd_output_silence(CdOutput* o, int queued);

/**
 * @brief Stop CD-owned sound and clear local readiness; retain the shared service.
 * @param o Output state.
 */
void cd_output_close(CdOutput* o);
