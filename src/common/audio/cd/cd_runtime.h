#pragma once

#include "audio/cd/cd_status.h"
#include <stdint.h>

/**
 * @brief Worker callbacks; invoked synchronously on the single CD thread.
 */
typedef struct
{
    const volatile int* running;                                  /**< Worker lifetime flag checked during blocking startup. */
    void (*stage)(CdPlaybackStatus* status, const char* message); /**< Copy progress text to status and the shared snapshot. */
    void (*clear_audio)(void);                                    /**< Clear analysis history under the worker's shared lock. */
    void (*pcm)(const uint8_t* samples, unsigned frames);         /**< Copy submitted PCM under the shared lock using a fresh timestamp. */
} CdRuntime;
