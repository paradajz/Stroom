#include "ui/artwork/worker.h"
#include "ui/presentation.h"
#include "platform/thread/worker.h"
#include "platform/thread/scheduler.h"
#include <stdlib.h>
#include <string.h>

#include "platform/time/clock.h"
#include "platform/time/sleep.h"

#define ARTWORK_WORKER_STACK_BYTES 16384

#define MICROSECONDS_PER_MILLISECOND 1000

static unsigned char stack[ARTWORK_WORKER_STACK_BYTES] __attribute__((aligned(16)));
static ArtworkImage  result_image __attribute__((aligned(64)));
static ArtworkBlob   pending;
static TrackMetadata result_metadata;
static int           result_valid, result_ready, stopping, decoding;
static Ps2Worker     background = PS2_WORKER_INITIALIZER;
#if STROOM_DIAGNOSTICS
static ArtworkObserver observer;

void artwork_worker_set_observer(ArtworkObserver sink)
{
    observer = sink;
}

static void observe(const TrackMetadata* metadata, ArtworkPhase phase, uint32_t begin, uint32_t end, unsigned a, unsigned b, unsigned c)
{
    if (observer)
    {
        observer(metadata, (ArtworkObservation){ phase, begin, end, { a, b, c } });
    }
}
#endif

/** @brief Decode outside the mailbox lock, leaving completed pixels untouched until taken. */
static void decode_worker(void* argument)
{
    (void)argument;

    for (;;)
    {
        platform_worker_wait(&background);
        platform_worker_lock(&background);

        if (stopping)
        {
            ArtworkBlob discarded = pending;

            memset(&pending, 0, sizeof(pending));
            platform_worker_unlock(&background);
            free(discarded.data);
            break;
        }

        if (result_ready || !pending.data)
        {
            platform_worker_unlock(&background);
            continue;
        }

        decoding = 1;

        ArtworkBlob job = pending;

        memset(&pending, 0, sizeof(pending));
        platform_worker_unlock(&background);
#if STROOM_DIAGNOSTICS
        uint32_t begin = platform_millis();

        observe(&job.metadata, DIAGNOSTIC_ARTWORK_DECODE_BEGIN, begin, begin, job.size, 0, 0);

        begin = platform_millis();
#endif
        int valid = artwork_decode(job.data, job.size, &result_image) == 0;
#if STROOM_DIAGNOSTICS
        uint32_t end = platform_millis();

        observe(&job.metadata, DIAGNOSTIC_ARTWORK_DECODE_END, begin, end, result_image.source_width, result_image.source_height, result_image.format | (valid ? 0x100u : 0));

        begin = platform_millis();
#endif
        free(job.data);
#if STROOM_DIAGNOSTICS
        observe(&job.metadata, DIAGNOSTIC_ARTWORK_DECODE_RELEASE, begin, platform_millis(), job.size, 0, 0);
#endif
        platform_worker_lock(&background);

        result_metadata = job.metadata;
        result_valid    = valid;
        result_ready    = 1;
        decoding        = 0;

        platform_worker_unlock(&background);
        /* A queued job must wait until the UI has copied this result. */
    }

    platform_worker_finish(&background);
}

int artwork_worker_open(void)
{
    if (background.running)
    {
        return 0;
    }

    /* A failed join retains resources; do not reuse them for a new decoder. */

    if (background.thread >= 0 || background.lock >= 0 || background.done >= 0 || background.wake >= 0)
    {
        return ARTWORK_WORKER_ERROR_ALREADY_OPEN;
    }

    int priority = platform_scheduler_background_priority();

    if (priority < 0)
    {
        return ARTWORK_WORKER_ERROR_PRIORITY;
    }

    result_ready = stopping = decoding = 0;
    const Ps2WorkerConfig config       = { .entry = decode_worker, .stack = stack, .stack_bytes = sizeof(stack), .priority = priority, .notifications = 1 };

    return platform_worker_open(&background, &config, NULL) == 0 ? 0 : ARTWORK_WORKER_ERROR_START;
}

int artwork_worker_close(void)
{
    if (background.lock >= 0)
    {
        platform_worker_lock(&background);

        stopping = 1;

        platform_worker_unlock(&background);
    }

    int result = platform_worker_close(&background);

    if (result != 0)
    {
        return result < 0 ? ARTWORK_WORKER_ERROR_CLOSE : result;
    }

    result_ready = 0;

    return 0;
}

int artwork_worker_submit(ArtworkBlob* blob)
{
    if (!background.running)
    {
        return ARTWORK_WORKER_ERROR_UNAVAILABLE;
    }

    if (!blob || !blob->data)
    {
        return ARTWORK_WORKER_ERROR_INVALID_BLOB;
    }

    platform_worker_lock(&background);

    ArtworkBlob discarded = pending;

    pending = *blob;

    memset(blob, 0, sizeof(*blob));
    platform_worker_unlock(&background);
    free(discarded.data);
    platform_worker_notify(&background);

    return 0;
}

int artwork_worker_take(const TrackMetadata* metadata, ArtworkImage* image, int* valid, ArtworkIdentity identity)
{
    if (!background.running)
    {
        return 1;
    }

    platform_worker_lock(&background);

    int available = result_ready;

    platform_worker_unlock(&background);

    if (!available)
    {
        return 1;
    }

    /* The worker cannot reuse its pixels while result_ready is set. Only the UI takes results. */
    int matches = track_artwork_equal(metadata, &result_metadata, identity);

    if (matches)
    {
        *image = result_image;
        *valid = result_valid;
    }

    platform_worker_lock(&background);

    result_ready = 0;

    platform_worker_unlock(&background);
    platform_worker_notify(&background);

    return matches ? 0 : 1;
}

void artwork_worker_yield(uint32_t frame_started)
{
    if (!background.running)
    {
        return;
    }

    platform_worker_lock(&background);

    int runnable = decoding || (pending.data && !result_ready);

    platform_worker_unlock(&background);

    if (!runnable)
    {
        return;
    }

    uint32_t elapsed = platform_millis() - frame_started;

    unsigned budget = ui_background_work_budget(elapsed);

    if (budget)
    {
        platform_sleep_us(budget * MICROSECONDS_PER_MILLISECOND);
    }
}
