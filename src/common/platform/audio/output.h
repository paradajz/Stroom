#pragma once

#include "util/diagnostics.h"
#include <stddef.h>
#include <stdint.h>

/** Failure codes for this API. */
typedef enum
{
    PS2_AUDIO_ERROR_VOLUME           = -1,
    PS2_AUDIO_ERROR_DRIVER_LOAD      = -2,
    PS2_AUDIO_ERROR_SERVICE_LOAD     = -3,
    PS2_AUDIO_ERROR_INITIALIZE       = -4,
    PS2_AUDIO_ERROR_FORMAT           = -5,
    PS2_AUDIO_ERROR_RPC_UNAVAILABLE  = -6,
    PS2_AUDIO_ERROR_PREPARE          = -7,
    PS2_AUDIO_ERROR_CLEANUP_PENDING  = -8,
    PS2_AUDIO_ERROR_LOCK_CREATE      = -9,
    PS2_AUDIO_ERROR_LOCK_UNAVAILABLE = -10,
    PS2_AUDIO_ERROR_INVALID_WRITE    = -11,
    PS2_AUDIO_ERROR_QUERY            = -12,
    PS2_AUDIO_ERROR_WRITE            = -13,
    PS2_AUDIO_ERROR_STOP             = -14,
    PS2_AUDIO_ERROR_QUIT             = -15,
    PS2_AUDIO_ERROR_LOCK_DELETE      = -16,
    PS2_AUDIO_ERROR_CANCELLED        = -17,
} Ps2AudioOutputError;

/* Admit a write when the pre-write queue is at most this threshold.
 * The accepted block may raise the queue above it. This value also bounds
 * each submitted block and the silent lead-in left by output preparation. */
#define PS2_AUDIO_OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES 4096

/* Silent block used while clearing the hardware ring during preparation. */
#define PS2_AUDIO_OUTPUT_SILENCE_BYTES 2048

/** @brief Opaque nonzero producer token assigned by the audio layer. */
typedef unsigned Ps2AudioProducer;
#define PS2_AUDIO_OUTPUT_NONE 0u

/**
 * @brief Worker callbacks used during cancellable device preparation.
 */
typedef struct
{
    const volatile int* running;                       /**< Worker lifetime flag. */
    void (*stage)(void* context, const char* message); /**< Optional progress callback. */
    void*  context;                                    /**< Callback context. */
    char*  error;                                      /**< Error destination. */
    size_t capacity;                                   /**< Error destination size. */
} Ps2AudioOutputRuntime;

/**
 * @brief Create device synchronization before workers start.
 * @return 0 on success, a negative Ps2AudioOutputError on failure.
 */
int platform_audio_output_open(void);

/**
 * @brief Select the only permitted producer without performing device I/O.
 * @param owner Desired producer.
 */
void platform_audio_output_select(Ps2AudioProducer owner);

/**
 * @brief Request volume mute; producers apply it without changing submitted PCM.
 * @param muted Nonzero for silent output.
 */
void platform_audio_output_set_muted(int muted);

/**
 * @brief Read whether a producer is selected.
 * @param owner Producer.
 * @return Nonzero when selected.
 */
int platform_audio_output_selected(Ps2AudioProducer owner);

/**
 * @brief Initialize the shared service once, without interrupting another producer.
 * @param runtime Progress and cancellation callbacks.
 * @return 0 on success, a negative Ps2AudioOutputError on failure.
 */
int platform_audio_output_initialize(const Ps2AudioOutputRuntime* runtime);

/**
 * @brief Acquire output and flush stale sound while muted.
 * @param owner Selected producer.
 * @param runtime Cancellation callbacks.
 * @return 0 when prepared, positive when unselected, negative on preparation failure.
 */
int platform_audio_output_prepare(Ps2AudioProducer owner, const Ps2AudioOutputRuntime* runtime);

/**
 * @brief Stop a producer only if it still owns the device.
 * @param owner Producer.
 * @return 0 on success, a negative Ps2AudioOutputError on failure.
 */
int platform_audio_output_stop(Ps2AudioProducer owner);

/**
 * @brief Submit bounded PCM without waiting for queue space.
 * @param owner Producer.
 * @param pcm Stereo PCM16 at 48 kHz.
 * @param bytes Byte count.
 * @return 0 when written, positive when busy/unselected, a negative Ps2AudioOutputError on failure.
 */
int platform_audio_output_write(Ps2AudioProducer owner, const uint8_t* pcm, unsigned bytes);

/**
 * @brief Timings of existing output operations; collecting them issues no extra sound RPC.
 */
typedef struct
{
    int      queued;       /**< Freshly observed device bytes, or minus one if not queried. */
    uint32_t begin;        /**< EE time before waiting for the output lock. */
    uint32_t end;          /**< EE time after releasing the output lock. */
    uint32_t lock_ms;      /**< Output lock wait. */
    uint32_t queued_ms;    /**< audsrv_queued call duration. */
    uint32_t available_ms; /**< audsrv_available call duration. */
    uint32_t submit_ms;    /**< audsrv_play_audio call duration. */
} Ps2AudioOutputWriteTiming;

#if STROOM_DIAGNOSTICS
/**
 * @brief Submit audio and measure the existing lock and sound calls.
 * @param owner Producer.
 * @param pcm Stereo PCM16 at 48 kHz.
 * @param bytes Byte count.
 * @param timing Observation destination.
 * @return 0 when written, positive when busy/unselected, a negative Ps2AudioOutputError on failure.
 */
int platform_audio_output_write_timed(Ps2AudioProducer owner, const uint8_t* pcm, unsigned bytes, Ps2AudioOutputWriteTiming* timing);
#endif

/**
 * @brief Read the queue snapshot, refreshed on sound-service notifications.
 * @param owner Producer.
 * @return Conservatively tracked queued bytes, zero when unowned, or a negative Ps2AudioOutputError.
 */
int platform_audio_output_queued(Ps2AudioProducer owner);

/**
 * @brief Release the shared service after all producers have stopped.
 * @return 0 on success, a negative Ps2AudioOutputError on failure.
 */
int platform_audio_output_close(void);
