#include "util/diagnostics.h"

/* One CD worker owns playback and hardware; the main thread exchanges requests and snapshots. */
#include "audio/cd/cd.h"
#include "audio/common/pcm.h"
#include "audio/output/device.h"
#include "audio/cd/cd_transport.h"
#include "audio/cd/cd_controller.h"
#include "platform/time/clock.h"
#include "platform/time/sleep.h"
#include "audio/common/worker_config.h"
#include "platform/thread/worker.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define CD_COMMAND_CAPACITY 16

static int              startup_autoplay;
static int              startup_succeeded;
static unsigned char    stack[AUDIO_WORKER_STACK_BYTES] __attribute__((aligned(16)));
static Ps2Worker        background = PS2_WORKER_INITIALIZER;
static AudioBuffer      incoming;
static CdPlaybackStatus published;
static CdToc            published_toc;

static AudioTransportCommand commands[CD_COMMAND_CAPACITY];
static unsigned              command_read, command_count;
static int                   requested_scan;
static int                   program_tracks[CD_MAX_TRACKS];
static unsigned              program_count;
static void                  publish(const CdPlaybackStatus* status);

/**
 * @brief Publish CD initialization progress and log changed messages.
 *
 * @param status Worker status to update.
 * @param message Progress message.
 */
static void stage(CdPlaybackStatus* status, const char* message)
{
    static char previous[AUDIO_STATUS_TEXT_BYTES];

    if (strcmp(previous, message) != 0)
    {
        STROOM_LOG("CD %s", message);
        snprintf(previous, sizeof(previous), "%s", message);
    }

    snprintf(status->status, sizeof(status->status), "%s", message);
    publish(status);
}

/**
 * @brief Copy worker disc status under the shared lock.
 *
 * @param status Playback status to copy into shared state.
 */
static void publish(const CdPlaybackStatus* status)
{
    platform_worker_lock(&background);

    if (published.generation != status->generation)
    {
        memset(&published_toc, 0, sizeof(published_toc));

        command_read = command_count = 0;
        requested_scan               = 0;
        program_count                = 0;

        memset(&incoming, 0, sizeof(incoming));
    }

    published = *status;

    platform_worker_unlock(&background);
}

/**
 * @brief Clear shared CD analysis buffers under the lock.
 */
static void clear_audio(void)
{
    platform_worker_lock(&background);
    memset(&incoming, 0, sizeof(incoming));
    platform_worker_unlock(&background);
}

/**
 * @brief Copy successfully submitted sound samples into shared analysis buffers.
 * @param samples Interleaved little-endian stereo PCM16 at AUDIO_RATE.
 * @param frames Number of stereo frames.
 */
static void capture_pcm(const uint8_t* samples, unsigned frames)
{
    platform_worker_lock(&background);
    /* Read the clock after any sound preparation and semaphore wait. */
    audio_push_pcm(&incoming, samples, frames, platform_millis());
    platform_worker_unlock(&background);
}

/**
 * @brief Copy pending requests under the lock, consuming at most one queued command.
 * @return Requests for this worker iteration.
 */
static CdRequests take_requests(void)
{
    CdRequests requests = { 0 };

    platform_worker_lock(&background);

    requests.generation     = published.generation;
    requests.scan_direction = requested_scan;
    requests.program_count  = program_count;

    memcpy(requests.program, program_tracks, program_count * sizeof(*program_tracks));

    program_count         = 0;
    requests.have_command = command_count != 0;

    if (requests.have_command)
    {
        requests.command = commands[command_read];
        command_read     = (command_read + 1) % CD_COMMAND_CAPACITY;

        --command_count;
    }

    platform_worker_unlock(&background);

    return requests;
}

/**
 * @brief Coordinate CD components on their sole owning thread.
 * @param arg Unused thread argument.
 */
static void worker(void* arg)
{
    (void)arg;

    const CdRuntime runtime = { &background.running, stage, clear_audio, capture_pcm };
    CdController    controller;

    cd_controller_open(&controller, &runtime, platform_millis(), startup_autoplay);
    publish(&controller.status);

    while (background.running && controller.drive_ready)
    {
        uint32_t now = platform_millis();

        cd_controller_detect(&controller, now);
        /* Detection can finish before the main thread hands sound ownership
         * to CD. Keep autoplay pending until that handoff is complete. */

        if (!controller.status.present || output_selected(OUTPUT_CD))
        {
            CdRequests requests = take_requests();

            cd_controller_step(&controller, &requests, now);
        }

        publish(&controller.status);
        platform_worker_lock(&background);

        published_toc = controller.toc;

        platform_worker_unlock(&background);
        platform_sleep_us(AUDIO_WORKER_IDLE_US);
    }

    cd_controller_close(&controller);
    publish(&controller.status);
    platform_worker_finish(&background);
}

int cd_open(int autoplay)
{
    if (background.thread >= 0 || background.lock >= 0 || background.done >= 0 || background.wake >= 0)
    {
        return CD_ERROR_ALREADY_OPEN;
    }

    startup_autoplay = autoplay;

    memset(&incoming, 0, sizeof(incoming));
    memset(&published, 0, sizeof(published));
    memset(&published_toc, 0, sizeof(published_toc));

    published.checking = 1;

    snprintf(published.status, sizeof(published.status), "STARTING CD DETECTION");

    command_read = command_count = 0;
    requested_scan               = 0;
    program_count                = 0;

    const Ps2WorkerConfig config = { .entry = worker, .stack = stack, .stack_bytes = sizeof(stack), .priority = AUDIO_WORKER_PRIORITY };

    Ps2WorkerError error;

    startup_succeeded = platform_worker_open(&background, &config, &error) == 0;

    if (!startup_succeeded)
    {
        published.checking = 0;

        snprintf(published.error, sizeof(published.error), "CD WORKER START FAILED");

        published.status[0] = 0;

        STROOM_LOG("CD worker startup %s failed: %d", error.stage, error.code);
    }

    return startup_succeeded ? 0 : CD_ERROR_WORKER_START;
}

int cd_poll(CdPlaybackStatus* status)
{
    if (!startup_succeeded || background.lock < 0)
    {
        memset(status, 0, sizeof(*status));
        snprintf(status->error, sizeof(status->error), "CD WORKER START FAILED");
        return CD_ERROR_WORKER_UNAVAILABLE;
    }

    platform_worker_lock(&background);
    audio_analyze(&incoming, platform_millis());

    *status = published;

    int available = background.running;

    platform_worker_unlock(&background);

    if (status->restart_required)
    {
        return CD_ERROR_RESTART_REQUIRED;
    }

    return available ? 0 : CD_ERROR_WORKER_UNAVAILABLE;
}

void cd_copy_snapshot(CdPlaybackStatus* status, Audio* audio)
{
    if (!startup_succeeded || background.lock < 0)
    {
        memset(audio, 0, sizeof(*audio));
        memset(status, 0, sizeof(*status));
        snprintf(status->error, sizeof(status->error), "CD WORKER START FAILED");
        return;
    }

    platform_worker_lock(&background);
    audio_analyze(&incoming, platform_millis());

    *status = published;

    if (status->present)
    {
        *audio = incoming.snapshot;
    }
    else
    {
        memset(audio, 0, sizeof(*audio));
    }

    platform_worker_unlock(&background);
}

void cd_transport_command(unsigned generation, AudioTransportCommand command)
{
    if (!startup_succeeded || background.lock < 0)
    {
        return;
    }

    platform_worker_lock(&background);

    if (!published.present || generation != published.generation)
    {
        platform_worker_unlock(&background);
        return;
    }

    if (command_count < CD_COMMAND_CAPACITY)
    {
        commands[(command_read + command_count) % CD_COMMAND_CAPACITY] = command;

        ++command_count;
    }

    platform_worker_unlock(&background);
}

void cd_transport_program(unsigned generation, const int* tracks, unsigned count)
{
    if (!startup_succeeded || background.lock < 0 || !count || count > CD_MAX_TRACKS)
    {
        return;
    }

    platform_worker_lock(&background);

    if (!published.present || generation != published.generation)
    {
        platform_worker_unlock(&background);
        return;
    }

    memcpy(program_tracks, tracks, count * sizeof(*tracks));

    program_count = count;

    platform_worker_unlock(&background);
}

void cd_transport_scan(unsigned generation, int direction)
{
    if (!startup_succeeded || background.lock < 0)
    {
        return;
    }

    platform_worker_lock(&background);

    if (!published.present || generation != published.generation)
    {
        platform_worker_unlock(&background);
        return;
    }

    /* Latest held state replaces the old one; release cannot sit behind a
     * backlog of repeated seek commands while a disc read is completing. */
    requested_scan = (direction > 0) - (direction < 0);

    platform_worker_unlock(&background);
}

int cd_close(void)
{
    int result = platform_worker_close(&background);

    if (result != 0)
    {
        return result < 0 ? CD_ERROR_WORKER_CLOSE : result;
    }

    startup_succeeded = 0;

    return 0;
}

int cd_copy_toc(unsigned generation, CdToc* toc)
{
    memset(toc, 0, sizeof(*toc));

    if (!startup_succeeded || background.lock < 0)
    {
        return 1;
    }

    platform_worker_lock(&background);

    int valid = published.present && generation == published.generation && published.tracks > 0 && published_toc.count == published.tracks;

    if (valid)
    {
        *toc = published_toc;
    }

    platform_worker_unlock(&background);

    return valid ? 0 : 1;
}
