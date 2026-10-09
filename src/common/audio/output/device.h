#pragma once

#include "platform/audio/output.h"

/** Failure codes for this API. */
typedef enum
{
    OUTPUT_ERROR_VOLUME           = PS2_AUDIO_ERROR_VOLUME,
    OUTPUT_ERROR_DRIVER_LOAD      = PS2_AUDIO_ERROR_DRIVER_LOAD,
    OUTPUT_ERROR_SERVICE_LOAD     = PS2_AUDIO_ERROR_SERVICE_LOAD,
    OUTPUT_ERROR_INITIALIZE       = PS2_AUDIO_ERROR_INITIALIZE,
    OUTPUT_ERROR_FORMAT           = PS2_AUDIO_ERROR_FORMAT,
    OUTPUT_ERROR_RPC_UNAVAILABLE  = PS2_AUDIO_ERROR_RPC_UNAVAILABLE,
    OUTPUT_ERROR_PREPARE          = PS2_AUDIO_ERROR_PREPARE,
    OUTPUT_ERROR_CLEANUP_PENDING  = PS2_AUDIO_ERROR_CLEANUP_PENDING,
    OUTPUT_ERROR_LOCK_CREATE      = PS2_AUDIO_ERROR_LOCK_CREATE,
    OUTPUT_ERROR_LOCK_UNAVAILABLE = PS2_AUDIO_ERROR_LOCK_UNAVAILABLE,
    OUTPUT_ERROR_INVALID_WRITE    = PS2_AUDIO_ERROR_INVALID_WRITE,
    OUTPUT_ERROR_QUERY            = PS2_AUDIO_ERROR_QUERY,
    OUTPUT_ERROR_WRITE            = PS2_AUDIO_ERROR_WRITE,
    OUTPUT_ERROR_STOP             = PS2_AUDIO_ERROR_STOP,
    OUTPUT_ERROR_QUIT             = PS2_AUDIO_ERROR_QUIT,
    OUTPUT_ERROR_LOCK_DELETE      = PS2_AUDIO_ERROR_LOCK_DELETE,
    OUTPUT_ERROR_CANCELLED        = PS2_AUDIO_ERROR_CANCELLED,
} OutputError;

/* Hardware admission limit; defined by the platform adapter. */
#define OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES PS2_AUDIO_OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES

/* Submit this silent tail after the last PCM before stopping audsrv, allowing
 * queued samples to play and preventing stale ring contents from repeating. */
#define OUTPUT_SILENCE_BYTES   PS2_AUDIO_OUTPUT_SILENCE_BYTES
#define OUTPUT_LEAD_OUT_BLOCKS 8

/**
 * @brief Exclusive sound-output owner selected by the source coordinator.
 */
typedef enum
{
    OUTPUT_NONE,
    OUTPUT_CD,
    OUTPUT_NETWORK
} OutputOwner;

/**
 * @brief Worker callbacks used during cancellable device preparation.
 */
typedef Ps2AudioOutputRuntime OutputRuntime;

/**
 * @brief Create device synchronization before workers start.
 * @return 0 on success, a negative OutputError on failure.
 */
int output_open(void);

/**
 * @brief Select the only permitted producer without performing device I/O.
 * @param owner Desired producer.
 */
void output_select(OutputOwner owner);

/**
 * @brief Forward requested mute without changing PCM or analysis.
 * @param muted Nonzero for silent output.
 */
void output_set_muted(int muted);

/**
 * @brief Read whether a producer is selected.
 * @param owner Producer.
 * @return Nonzero when selected.
 */
int output_selected(OutputOwner owner);

/**
 * @brief Initialize the shared service once, without interrupting another producer.
 * @param runtime Progress and cancellation callbacks.
 * @return 0 on success, a negative OutputError on failure.
 */
int output_initialize(const OutputRuntime* runtime);

/**
 * @brief Acquire output and flush stale sound while muted.
 * @param owner Selected producer.
 * @param runtime Cancellation callbacks.
 * @return 0 when prepared, positive when unselected, negative on preparation failure.
 */
int output_prepare(OutputOwner owner, const OutputRuntime* runtime);

/**
 * @brief Stop a producer only if it still owns the device.
 * @param owner Producer.
 * @return 0 on success, a negative OutputError on failure.
 */
int output_stop(OutputOwner owner);

/**
 * @brief Submit bounded PCM without waiting for queue space.
 * @param owner Producer.
 * @param pcm Stereo PCM16 at 48 kHz.
 * @param bytes Byte count.
 * @return 0 when submitted, positive when busy or unselected, a negative OutputError on failure.
 */
int output_write(OutputOwner owner, const uint8_t* pcm, unsigned bytes);

/**
 * @brief Timings of existing output operations; collecting them issues no extra sound RPC.
 */
typedef Ps2AudioOutputWriteTiming OutputWriteTiming;

#if STROOM_DIAGNOSTICS
/**
 * @brief Submit audio and measure the existing lock and sound calls.
 * @param owner Producer.
 * @param pcm Stereo PCM16 at 48 kHz.
 * @param bytes Byte count.
 * @param timing Observation destination.
 * @return 0 when submitted, positive when busy or unselected, a negative OutputError on failure.
 */
int output_write_timed(OutputOwner owner, const uint8_t* pcm, unsigned bytes, OutputWriteTiming* timing);
#endif

/**
 * @brief Read the queue snapshot, refreshed on sound-service notifications.
 * @param owner Producer.
 * @return Conservatively tracked queued bytes, zero when unowned, or a negative OutputError.
 */
int output_queued(OutputOwner owner);

/**
 * @brief Release the shared service after both workers have joined.
 * @return 0 on success, a negative OutputError on failure.
 */
int output_close(void);
