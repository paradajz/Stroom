#include "audio/cd/cd_output.h"
#include "audio/output/device.h"
#include <string.h>

static uint8_t       pcm[CD_OUTPUT_BYTES] __attribute__((aligned(64)));
static const uint8_t silence[OUTPUT_SILENCE_BYTES];

/**
 * @brief CD context for shared sound-service progress reports.
 */
typedef struct
{
    const CdRuntime*  runtime; /**< CD callbacks. */
    CdPlaybackStatus* status;  /**< CD status. */
} CdOutputProgress;

/**
 * @brief Forward progress.
 * @param context CD callback state.
 * @param message Progress text.
 */
static void stage(void* context, const char* message)
{
    CdOutputProgress* p = context;

    p->runtime->stage(p->status, message);
}

int cd_output_open(CdOutput* o, const CdRuntime* runtime, CdPlaybackStatus* status)
{
    if (!o->ready)
    {
        CdOutputProgress progress = { runtime, status };
        OutputRuntime    output   = { runtime->running, stage, &progress, status->error, sizeof(status->error) };

        int result = output_initialize(&output);
        o->ready   = result == 0;

        return result;
    }

    return 0;
}

int cd_output_stop(const CdOutput* o)
{
    return o->ready ? output_stop(OUTPUT_CD) : 0;
}

int cd_output_reset(CdOutput* o, const CdRuntime* runtime, int resume)
{
    OutputRuntime output = { .running = runtime->running };
    int           result = o->ready && resume ? output_prepare(OUTPUT_CD, &output) : 0;

    o->block_write = o->block_count = 0;
    o->pending                      = 0;

    memset(&o->resampler, 0, sizeof(o->resampler));

    return result;
}

/**
 * @brief Remember a successful write for queued CD-byte accounting.
 * @param o Output submission history.
 * @param bytes Submitted bytes.
 * @param audio Nonzero for CD samples, zero for silence.
 */
static void record_block(CdOutput* o, int bytes, int audio)
{
    o->blocks[o->block_write] = (CdOutputBlock){ bytes, audio };
    o->block_write            = (o->block_write + 1) % CD_OUTPUT_BLOCKS;

    if (o->block_count < CD_OUTPUT_BLOCKS)
    {
        ++o->block_count;
    }
}

int cd_output_submit(CdOutput* o, const uint8_t* sector, const uint8_t** samples)
{
    if (!o->pending)
    {
        o->pending_resampler = o->resampler;

        cd_resample(&o->pending_resampler, sector, pcm);

        o->pending = 1;
    }

    int result = output_write(OUTPUT_CD, pcm, sizeof(pcm));

    if (result != 0)
    {
        return result;
    }

    o->resampler = o->pending_resampler;
    o->pending   = 0;

    record_block(o, sizeof(pcm), 1);

    *samples = pcm;

    return 0;
}

int cd_output_queued(const CdOutput* o, int* audio_bytes)
{
    int queued    = output_queued(OUTPUT_CD);
    int remaining = queued;

    *audio_bytes = 0;

    /* The hardware queue is the remaining suffix of successful writes.
     * Anything preceding this history is the silent preparation lead-in. */

    for (unsigned i = 0; i < o->block_count && remaining > 0; ++i)
    {
        unsigned             index = (o->block_write + CD_OUTPUT_BLOCKS - 1 - i) % CD_OUTPUT_BLOCKS;
        const CdOutputBlock* block = &o->blocks[index];
        int                  bytes = remaining < block->bytes ? remaining : block->bytes;

        if (block->audio)
        {
            *audio_bytes += bytes;
        }

        remaining -= bytes;
    }

    return queued;
}

int cd_output_silence(CdOutput* o, int queued)
{
    if (queued < 0)
    {
        return queued;
    }

    if (queued > OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES)
    {
        return 1;
    }

    int result = output_write(OUTPUT_CD, silence, sizeof(silence));

    if (result != 0)
    {
        return result;
    }

    record_block(o, sizeof(silence), 0);

    return 0;
}

void cd_output_close(CdOutput* o)
{
    o->pending = 0;

    if (o->ready)
    {
        output_stop(OUTPUT_CD);

        o->ready = 0;
    }
}
