#pragma once

#include "audio/common/snapshot.h"

#define AUDIO_CAPACITY 4096

/**
 * @brief Producer-owned PCM queue, analysis state and latest audio snapshot.
 */
typedef struct
{
    Audio    snapshot;                /**< Recent samples and levels copied to consumers. */
    int16_t  ring[AUDIO_CAPACITY][2]; /**< PCM frames awaiting level analysis. */
    unsigned read;                    /**< Next queued frame to analyze. */
    unsigned count;                   /**< Number of queued frames. */
    uint32_t last_ms;                 /**< Last accepted PCM timestamp in monotonic milliseconds. */
} AudioBuffer;

/**
 * @brief Queue stereo PCM16 and retain analysis history, dropping oldest frames on overflow.
 *
 * @param a Audio buffers to update.
 * @param pcm Interleaved little-endian PCM16 at AUDIO_RATE; four bytes per frame.
 * @param frames Number of stereo frames.
 * @param now_ms Current monotonic time in milliseconds.
 */
void audio_push_pcm(AudioBuffer* a, const uint8_t* pcm, unsigned frames, uint32_t now_ms);

/**
 * @brief Consume queued frames to update RMS and sample peaks; expire idle audio after 250 ms.
 *
 * @param a Audio state to update.
 * @param now_ms Current monotonic time in milliseconds.
 */
void audio_analyze(AudioBuffer* a, uint32_t now_ms);
