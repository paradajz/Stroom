#include "audio/network/ariacast/playback.h"
#include <stdio.h>

int aria_playback_stop(AriaPlayback* playback)
{
    if (playback->started && playback->audible && output_stop(OUTPUT_NETWORK) != 0)
    {
        return ARIA_PLAYBACK_ERROR_STOP;
    }

    playback->tail_blocks = 0;
    playback->started     = 0;
    playback->silent      = 0;
    playback->buffering   = 0;

    return 0;
}

void aria_playback_step(AriaPlayback* playback, AriaStream* stream, AriaDiagnostics* diagnostics, const OutputRuntime* runtime, uint32_t now, void (*capture)(void* context, const uint8_t* pcm, unsigned frames), void* context)
{
    int      audible = !stream->listening;
    unsigned reserve = audible && !stream->ending ? ARIA_PREBUFFER_MESSAGES : 1;

    if (playback->generation != stream->generation || playback->audible != audible || !aria_stream_active(stream, now) || (audible && !output_selected(OUTPUT_NETWORK)))
    {
        if (playback->started)
        {
            int stopped = aria_playback_stop(playback) == 0;

            capture(context, NULL, 0);

            if (!stopped)
            {
                snprintf(runtime->error, runtime->capacity, "STREAM SOUND STOP ERROR");
                return;
            }
        }

        playback->generation = stream->generation;
    }

    if (!aria_stream_active(stream, now) || (audible && !output_selected(OUTPUT_NETWORK)))
    {
        return;
    }

    if (stream->ending && !stream->count &&
        (!playback->started || (!audible && (uint32_t)(now - playback->consumed_at) >= ARIA_PCM_MESSAGE_MS) || playback->tail_blocks >= OUTPUT_LEAD_OUT_BLOCKS))
    {
        if (aria_playback_stop(playback) != 0)
        {
            snprintf(runtime->error, runtime->capacity, "STREAM SOUND STOP ERROR");
            stream->disconnect(stream->context, "sound stop failed");
            return;
        }

        capture(context, NULL, 0);

        stream->finished = 1;

        return;
    }

    if (!playback->started)
    {
        if (stream->count < reserve)
        {
            return;
        }

        if (audible && (output_initialize(runtime) != 0 || output_prepare(OUTPUT_NETWORK, runtime) != 0))
        {
            if (!runtime->error[0])
            {
                snprintf(runtime->error, runtime->capacity, "STREAM SOUND START FAILED");
            }

            stream->disconnect(stream->context, "sound preparation failed");
            capture(context, NULL, 0);
            return;
        }

        playback->started     = 1;
        playback->audible     = audible;
        playback->consumed_at = now - ARIA_PCM_MESSAGE_MS;
    }

    if (!stream->count)
    {
        playback->buffering = 1;
    }

    if (playback->buffering && stream->count >= reserve)
    {
        playback->buffering = 0;
    }

    int pcm_ready = stream->count && !playback->buffering;

    if (!audible)
    {
        /* Sound normally provides the consumption clock. Listening is paced
         * by message duration, without a sound reserve or any sound RPCs. */

        if (!pcm_ready)
        {
            /* Empty between packets is normal here. Keep sample history;
             * audio_analyze expires genuinely stale input on its normal clock. */

            if ((uint32_t)(now - playback->consumed_at) >= ARIA_PCM_MESSAGE_MS)
            {
                playback->consumed_at = now - ARIA_PCM_MESSAGE_MS;
            }
        }
        else if ((uint32_t)(now - playback->consumed_at) >= ARIA_PCM_MESSAGE_MS)
        {
            playback->consumed_at += ARIA_PCM_MESSAGE_MS;
            playback->silent = 0;

            capture(context, stream->pcm[stream->read], ARIA_PCM_FRAMES);
            aria_stream_consume(stream);
        }

        return;
    }

    static const uint8_t silence[OUTPUT_SILENCE_BYTES];
    const uint8_t*       pcm   = pcm_ready ? stream->pcm[stream->read] : silence;
    unsigned             bytes = pcm_ready ? ARIA_PCM_BYTES : sizeof(silence);
#if STROOM_DIAGNOSTICS
    OutputWriteTiming timing;
    int               result = output_write_timed(OUTPUT_NETWORK, pcm, bytes, &timing);

    aria_diagnostics_output(diagnostics, timing.end, pcm_ready, timing.queued, result);

    if (result != 0 || (uint32_t)(timing.end - timing.begin) >= DIAGNOSTIC_CAPTURE_SLOW_MS)
    {
        AriaDiagnosticCaptureRecord record = { .at = timing.end, .kind = DIAGNOSTIC_CAPTURE_OUTPUT, .data = { [DIAGNOSTIC_CAPTURE_OUTPUT_PCM] = pcm_ready, [DIAGNOSTIC_CAPTURE_OUTPUT_RESULT] = (uint32_t)result, [DIAGNOSTIC_CAPTURE_OUTPUT_QUEUED_BYTES] = (uint32_t)timing.queued, [DIAGNOSTIC_CAPTURE_OUTPUT_LOCK_MS] = timing.lock_ms, [DIAGNOSTIC_CAPTURE_OUTPUT_DURATION_MS] = timing.end - timing.begin, [DIAGNOSTIC_CAPTURE_OUTPUT_PCM_BYTES_SUBMITTED] = diagnostics->pcm_writes * ARIA_PCM_BYTES } };

        aria_diagnostic_capture_record(&diagnostics->diagnostic_capture, record);

        record.kind                                                   = DIAGNOSTIC_CAPTURE_SOUND_RPC;
        record.data[DIAGNOSTIC_CAPTURE_SOUND_RPC_QUEUED_MS]           = timing.queued_ms;
        record.data[DIAGNOSTIC_CAPTURE_SOUND_RPC_AVAILABLE_MS]        = timing.available_ms;
        record.data[DIAGNOSTIC_CAPTURE_SOUND_RPC_SUBMIT_MS]           = timing.submit_ms;
        record.data[DIAGNOSTIC_CAPTURE_SOUND_RPC_BYTES]               = bytes;
        record.data[DIAGNOSTIC_CAPTURE_SOUND_RPC_PCM_MESSAGES_QUEUED] = stream->count;
        record.data[DIAGNOSTIC_CAPTURE_SOUND_RPC_SILENCE_WRITES]      = diagnostics->silence_writes;

        aria_diagnostic_capture_record(&diagnostics->diagnostic_capture, record);
    }

    if (result == 0)
    {
        aria_diagnostic_capture_output(&diagnostics->diagnostic_capture, timing.end, stream->generation, pcm_ready);
    }
#else
    (void)diagnostics;

    int result = output_write(OUTPUT_NETWORK, pcm, bytes);
#endif

    if (result == 0 && stream->ending && !pcm_ready)
    {
        ++playback->tail_blocks;
    }

    if (result < 0)
    {
        snprintf(runtime->error, runtime->capacity, "STREAM SOUND OUTPUT ERROR");
        aria_playback_stop(playback);
        stream->disconnect(stream->context, "sound output failed");
        capture(context, NULL, 0);
    }
    else if (result == 0 && pcm_ready)
    {
        playback->silent = 0;

        capture(context, pcm, ARIA_PCM_FRAMES);
        aria_stream_consume(stream);
    }
    else if (result == 0 && !playback->silent)
    {
        playback->silent = 1;

        capture(context, NULL, 0);
    }
}
