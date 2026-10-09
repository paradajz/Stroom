#pragma once

#include "audio/cd/cd_status.h"
#include <stdint.h>

/** Source-side conditions observed while feeding the local sound clock. */
enum
{
    CD_ACTIVITY_DRIVE_WAIT  = 1u,
    CD_ACTIVITY_OUTPUT_BUSY = 2u,
    CD_ACTIVITY_SILENCE     = 4u
};

/**
 * @brief Worker callbacks; invoked synchronously on the single CD thread.
 */
typedef struct
{
    const volatile int* running;                                  /**< Worker lifetime flag checked during blocking startup. */
    void (*stage)(CdPlaybackStatus* status, const char* message); /**< Copy progress text to status and the shared snapshot. */
    void (*clear_audio)(void);                                    /**< Clear analysis history under the worker's shared lock. */
    void (*pcm)(const uint8_t* samples, unsigned frames);         /**< Copy submitted PCM under the shared lock using a fresh timestamp. */
    void (*stream_reset)(void);                                   /**< Cancel outgoing audio after a discontinuity; optional. */
    void (*stream_end)(void);                                     /**< Finish outgoing audio after normal playback; optional. */
    void (*stream_activity)(unsigned activity);                   /**< Source-side wait/silence observations; optional. */
} CdRuntime;
