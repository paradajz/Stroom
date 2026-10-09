#include "platform/cd/reader.h"
#include "platform/thread/worker.h"
#include "platform/time/clock.h"
#include "platform/time/sleep.h"
#include <stdio.h>
#include <string.h>

#define READER_STACK_BYTES 16384
#define READER_PRIORITY    2
#define READER_IDLE_US     1000
#define MEDIA_POLL_MS      500u
#define READ_RETRY_MS      100u
#define TOC_BYTES          2064

static Ps2Worker      background = PS2_WORKER_INITIALIZER;
static unsigned char  stack[READER_STACK_BYTES] __attribute__((aligned(16)));
static Ps2CdDrive     drive, published;
static const uint8_t* ready[PS2_CD_RESERVE_SECTORS];
static unsigned       head, count, consumed, epoch, handled_epoch;
static int            position, boundary, active, error, opened;
static Ps2CdMedia     media;
static int            media_known, media_changed;
static unsigned       media_serial;
static uint8_t        toc_data[TOC_BYTES];
static int            toc_requested, toc_ready, toc_result, toc_error;
static size_t         toc_size;

/* Only the reader calls the SDK. This lock protects memory exchanges only;
 * never hold it over a drive call, cancellation, or worker join. */
static void worker(void* argument)
{
    (void)argument;

    unsigned forwarded = 0;
    uint32_t checked   = platform_millis() - MEDIA_POLL_MS;
    uint32_t retry_at  = 0;
    int      retrying  = 0;

    while (background.running)
    {
        platform_worker_lock(&background);

        unsigned request_epoch = epoch;
        int      reset         = request_epoch != handled_epoch;
        int      start         = position;

        if (!reset)
        {
            while (consumed)
            {
                platform_cd_drive_consume(&drive);
                --consumed;
                --forwarded;
            }
        }

        platform_worker_unlock(&background);

        if (reset)
        {
            int result = platform_cd_drive_reset(&drive, start);
            forwarded  = 0;
            retrying   = 0;

            platform_worker_lock(&background);

            handled_epoch = request_epoch;

            if (epoch == request_epoch)
            {
                published = drive;

                if (result < 0)
                {
                    error = result;
                }
            }

            platform_worker_unlock(&background);
        }

        uint32_t now       = platform_millis();
        unsigned completed = drive.completed;
        int      result    = platform_cd_drive_poll(&drive, now);
        now                = platform_millis();

        if (result < 0)
        {
            retrying = 1;
            retry_at = now;
            checked  = now - MEDIA_POLL_MS;
        }

        platform_worker_lock(&background);

        if (epoch == handled_epoch)
        {
            if (drive.completed != completed)
            {
                error = 0;
            }

            if (result < 0 && !error)
            {
                error = result;
            }

            while (forwarded < drive.buffered && !drive.failed)
            {
                ready[(head + count) % PS2_CD_RESERVE_SECTORS] = platform_cd_drive_peek(&drive, forwarded);

                ++forwarded;
                ++count;
            }

            published = drive;
        }
        else if (drive.failed)
        {
            published.failed = 1;
        }

        platform_worker_unlock(&background);

        if ((uint32_t)(now - checked) >= MEDIA_POLL_MS && !drive.failed)
        {
            Ps2CdMedia observed = platform_cd_drive_media();
            checked             = platform_millis();

            platform_worker_lock(&background);

            int changed = observed.changed || (media_known && observed.type != media.type);
            media       = observed;
            media_known = 1;
            media_changed |= changed;

            if (changed)
            {
                ++media_serial;
                ++epoch;

                consumed      = 0;
                count         = 0;
                head          = 0;
                active        = 0;
                toc_ready     = 0;
                toc_requested = 0;
            }

            platform_worker_unlock(&background);
        }

        platform_worker_lock(&background);

        unsigned serial   = media_serial;
        int      read_toc = toc_requested && platform_cd_drive_idle(&drive) && epoch == handled_epoch;

        platform_worker_unlock(&background);

        if (read_toc)
        {
            Ps2CdToc raw;
            int      read_result = platform_cd_drive_toc(&raw);

            platform_worker_lock(&background);

            if (serial == media_serial && toc_requested)
            {
                toc_size = raw.size < sizeof(toc_data) ? raw.size : sizeof(toc_data);

                memcpy(toc_data, raw.data, toc_size);

                toc_error     = raw.error;
                toc_result    = read_result;
                toc_requested = 0;
                toc_ready     = 1;
            }

            platform_worker_unlock(&background);
        }

        platform_worker_lock(&background);

        request_epoch = epoch;
        int read      = active && request_epoch == handled_epoch;
        int end       = boundary;

        platform_worker_unlock(&background);

        now = platform_millis();

        if (read && (!retrying || (uint32_t)(now - retry_at) >= READ_RETRY_MS))
        {
            int submitted = platform_cd_drive_prefetch(&drive, end);
            retrying      = submitted < 0;

            if (retrying)
            {
                retry_at = platform_millis();
                checked  = retry_at - MEDIA_POLL_MS;

                platform_worker_lock(&background);

                if (epoch == request_epoch && !error)
                {
                    error = submitted;
                }

                platform_worker_unlock(&background);
            }
        }

        platform_sleep_us(READER_IDLE_US);
    }

    platform_cd_drive_close(&drive);
    platform_worker_lock(&background);

    published = drive;
    consumed  = 0;
    count     = 0;
    head      = 0;

    platform_worker_unlock(&background);
    platform_worker_finish(&background);
}

int platform_cd_reader_open(Ps2CdReader* reader, const Ps2CdDriveRuntime* runtime)
{
    memset(reader, 0, sizeof(*reader));

    if (opened || background.thread >= 0 || background.lock >= 0 || background.done >= 0)
    {
        snprintf(runtime->error, runtime->capacity, "CD READER STILL CLOSING");
        return PS2_CD_DRIVE_ERROR_WORKER_START;
    }

    int result    = platform_cd_drive_open(&drive, runtime);
    reader->state = drive;

    if (result < 0)
    {
        return result;
    }

    published     = drive;
    media_serial  = 0;
    handled_epoch = 0;
    epoch         = 0;
    consumed      = 0;
    count         = 0;
    head          = 0;
    media_changed = 0;
    media_known   = 0;
    error         = 0;
    active        = 0;
    boundary      = 0;
    position      = 0;
    toc_ready     = 0;
    toc_requested = 0;

    memset(&media, 0, sizeof(media));

    opened                       = 1;
    const Ps2WorkerConfig config = { .entry = worker, .stack = stack, .stack_bytes = sizeof(stack), .priority = READER_PRIORITY };

    if (platform_worker_open(&background, &config, NULL) != 0)
    {
        snprintf(runtime->error, runtime->capacity, "CD READER WORKER START FAILED");
        return PS2_CD_DRIVE_ERROR_WORKER_START;
    }

    return 0;
}

int platform_cd_reader_media(Ps2CdMedia* destination)
{
    platform_worker_lock(&background);

    *destination         = media;
    destination->changed = media_changed;
    media_changed        = 0;
    int pending          = !media_known;

    platform_worker_unlock(&background);

    return pending;
}

int platform_cd_reader_toc(Ps2CdToc* toc)
{
    platform_worker_lock(&background);

    int result = 1;

    if (toc_ready)
    {
        *toc      = (Ps2CdToc){ toc_data, toc_size, toc_error };
        result    = toc_result;
        toc_ready = 0;
    }
    else
    {
        toc_requested = 1;
    }

    platform_worker_unlock(&background);

    return result;
}

int platform_cd_reader_idle(const Ps2CdReader* reader)
{
    return platform_cd_drive_idle(&reader->state);
}

int platform_cd_reader_failed(const Ps2CdReader* reader)
{
    return reader->state.failed;
}

int platform_cd_reader_reset(Ps2CdReader* reader, int start)
{
    platform_worker_lock(&background);

    int failed = published.failed;

    if (!failed)
    {
        ++epoch;

        position                = start;
        consumed                = 0;
        count                   = 0;
        head                    = 0;
        error                   = 0;
        active                  = 0;
        reader->state.completed = 0;
        reader->state.buffered  = 0;
    }

    platform_worker_unlock(&background);

    return failed ? PS2_CD_DRIVE_ERROR_RESTART_REQUIRED : 0;
}

int platform_cd_reader_poll(Ps2CdReader* reader, uint32_t now)
{
    (void)now;
    platform_worker_lock(&background);

    if (epoch == handled_epoch)
    {
        reader->state = published;
    }

    reader->state.failed   = published.failed;
    reader->state.buffered = count;
    int result             = error;
    error                  = 0;

    platform_worker_unlock(&background);

    return reader->state.failed ? PS2_CD_DRIVE_ERROR_RESTART_REQUIRED : result;
}

const uint8_t* platform_cd_reader_sector(Ps2CdReader* reader)
{
    (void)reader;
    platform_worker_lock(&background);

    const uint8_t* sector = count && !published.failed ? ready[head] : NULL;

    platform_worker_unlock(&background);

    return sector;
}

unsigned platform_cd_reader_buffered(const Ps2CdReader* reader)
{
    (void)reader;
    platform_worker_lock(&background);

    unsigned available = published.failed ? 0 : count;

    platform_worker_unlock(&background);

    return available;
}

void platform_cd_reader_consume(Ps2CdReader* reader)
{
    (void)reader;
    platform_worker_lock(&background);

    if (count)
    {
        head = (head + 1) % PS2_CD_RESERVE_SECTORS;

        --count;
        ++consumed;
    }

    platform_worker_unlock(&background);
}

int platform_cd_reader_prefetch(Ps2CdReader* reader, int end)
{
    (void)reader;
    platform_worker_lock(&background);

    boundary   = end;
    active     = !media_changed;
    int failed = published.failed;

    platform_worker_unlock(&background);

    return failed ? PS2_CD_DRIVE_ERROR_RESTART_REQUIRED : 0;
}

int platform_cd_reader_waiting(const Ps2CdReader* reader)
{
    return !platform_cd_reader_buffered(reader);
}

void platform_cd_reader_suspend(Ps2CdReader* reader)
{
    (void)reader;
    platform_worker_lock(&background);

    active = 0;

    platform_worker_unlock(&background);
}

int platform_cd_reader_close(Ps2CdReader* reader)
{
    if (!opened)
    {
        return 0;
    }

    int result = platform_worker_close(&background);

    if (result == 0)
    {
        reader->state = published;
        opened        = 0;
    }

    return result;
}
