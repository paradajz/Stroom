#include "util/diagnostics.h"
#include "audio/cd/cd_controller.h"
#include "audio/output/device.h"
#include <stdio.h>
#include <string.h>

#define MEDIA_POLL_MS       500
#define TOC_RETRY_WINDOW_MS 10000

/**
 * @brief Stop output and clear analysis after an I/O error.
 * @param c Controller state.
 * @param message Error text.
 */
static void stream_error(CdController* c, const char* message)
{
    c->status.playing = c->status.paused = c->status.scanning = c->restart = 0;

    memset(&c->scan, 0, sizeof(c->scan));
    /* Best-effort cleanup; preserve the original I/O error if stopping also fails. */
    (void)cd_output_stop(&c->output);
    c->runtime->clear_audio();
    snprintf(c->status.error, sizeof(c->status.error), "%s", message);
}

/**
 * @brief Stop playback and preserve the fatal drive error until restart.
 * @param c Controller state.
 */
static void drive_unresponsive(CdController* c)
{
    c->status.restart_required = 1;

    stream_error(c, "CD DRIVE UNRESPONSIVE - RESTART REQUIRED");
}

/**
 * @brief Read the track table and initialize output for a newly detected disc.
 * @param c Controller state.
 * @param now Monotonic milliseconds used to seed track ordering.
 */
static void read_tracks(CdController* c, uint32_t now)
{
    if (!c->toc_attempts)
    {
        c->toc_started = now;
    }

    char message[AUDIO_STATUS_TEXT_BYTES];

    snprintf(message, sizeof(message), "READING CD TRACKS - ATTEMPT %u", ++c->toc_attempts);
    c->runtime->stage(&c->status, message);

    Ps2CdToc raw;
    int      result = platform_cd_drive_toc(&raw);

    STROOM_LOG("CD TOC result=%d error=%02x first=%02x last=%02x", result, raw.error, raw.data[CD_TOC_MSF_OFFSET], raw.data[CD_TOC_ENTRY_BYTES + CD_TOC_MSF_OFFSET]);

    int invalid_toc = 0;

    if (result == 0)
    {
        result      = cd_parse_toc(&c->toc, raw.data, raw.size);
        invalid_toc = result < 0;
    }

    if (result < 0)
    {
        c->toc.count = 0;

        if ((uint32_t)(now - c->toc_started) >= TOC_RETRY_WINDOW_MS)
        {
            if (invalid_toc)
            {
                snprintf(c->status.error, sizeof(c->status.error), "INVALID CD TRACK LIST - SELECT PLAY TO RETRY");
            }
            else
            {
                snprintf(c->status.error, sizeof(c->status.error), "CD TRACK LIST ERROR %02X - SELECT PLAY TO RETRY", (unsigned)raw.error & UINT8_MAX);
            }
        }
        else
        {
            c->runtime->stage(&c->status, "CD TRACK READ FAILED - RETRYING");
        }

        return;
    }

    c->status.tracks   = c->toc.count;
    c->playback.random = now;

    cd_playback_mode(&c->playback, CD_CONTINUE, c->toc.count, 1);

    c->status.track            = 1;
    c->position                = c->toc.start[0];
    c->status.duration_seconds = (c->toc.start[1] - c->toc.start[0]) / CD_SECTORS_PER_SECOND;

    c->runtime->stage(&c->status, "STARTING CD SOUND OUTPUT");

    if (cd_output_open(&c->output, c->runtime, &c->status) == 0)
    {
        c->status.playing = c->autoplay;

        c->runtime->stage(&c->status, "AUDIO CD READY");
    }

    c->restart = 1;
}

void cd_controller_detect(CdController* c, uint32_t now)
{
    if (!c->drive_ready || (uint32_t)(now - c->checked) < MEDIA_POLL_MS)
    {
        return;
    }

    c->checked = now;

    Ps2CdMedia media = platform_cd_drive_media();

    if (media.changed || !media.audio)
    {
        if (c->status.present)
        {
            c->restart = 1;
        }

        memset(&c->scan, 0, sizeof(c->scan));

        c->scan_inhibited = 0;

        memset(&c->status, 0, sizeof(c->status));

        c->status.checking = media.checking;

        char message[AUDIO_STATUS_TEXT_BYTES];

        if (media.checking)
        {
            snprintf(message, sizeof(message), "IDENTIFYING DISC - TYPE %02X", media.type);
        }
        else if (media.absent)
        {
            snprintf(message, sizeof(message), "NO DISC DETECTED");
        }
        else
        {
            snprintf(message, sizeof(message), "NOT AN AUDIO CD - TYPE %02X", media.type);
        }

        c->runtime->stage(&c->status, message);
    }

    /* Report insertion before potentially blocking TOC and sound initialization. */

    if (media.audio && !c->status.present)
    {
        c->status.present    = 1;
        c->status.checking   = 0;
        c->status.generation = ++c->generation;
        c->toc_attempts      = 0;

        memset(&c->playback, 0, sizeof(c->playback));
        c->runtime->stage(&c->status, "AUDIO CD DETECTED");
    }

    if (platform_cd_drive_failed(&c->drive))
    {
        c->status.restart_required = 1;

        snprintf(c->status.error, sizeof(c->status.error), "CD DRIVE UNRESPONSIVE - RESTART REQUIRED");
        return;
    }

    if (c->status.present && !c->status.tracks && !c->status.error[0] && platform_cd_drive_idle(&c->drive))
    {
        read_tracks(c, now);
    }
}

/**
 * @brief Apply a transport command, retrying startup when Play needs it.
 * @param c Controller state.
 * @param command Requested transport operation.
 * @param now Monotonic milliseconds for track-table retry scheduling.
 */
static void apply_command(CdController* c, AudioTransportCommand command, uint32_t now)
{
    if (command == AUDIO_TRANSPORT_PLAY_PAUSE && c->status.present)
    {
        if (!c->status.tracks)
        {
            c->toc_attempts    = 0;
            c->status.error[0] = 0;
            c->checked         = now - MEDIA_POLL_MS;

            c->runtime->stage(&c->status, "RETRYING CD TRACK READ");
        }
        else if (!c->output.ready)
        {
            c->status.error[0] = 0;

            c->runtime->stage(&c->status, "RETRYING CD SOUND OUTPUT");
            cd_output_open(&c->output, c->runtime, &c->status);

            if (c->output.ready)
            {
                c->runtime->stage(&c->status, "AUDIO CD READY");
            }
        }
    }

    if (c->status.present && c->status.tracks && c->output.ready)
    {
        if (command == AUDIO_TRANSPORT_PLAY_PAUSE)
        {
            if (c->status.playing)
            {
                c->status.playing = 0;
                c->status.paused  = 1;
            }
            else
            {
                if (!c->status.paused && c->position >= c->toc.start[c->status.track])
                {
                    c->position = c->toc.start[c->status.track - 1];
                }

                c->status.playing  = 1;
                c->status.paused   = 0;
                c->status.error[0] = 0;
            }
        }
        else if (command == AUDIO_TRANSPORT_STOP)
        {
            memset(&c->scan, 0, sizeof(c->scan));

            c->status.scanning = 0;
            c->scan_inhibited  = 1;
            c->status.playing = c->status.paused = 0;
            c->position                          = c->toc.start[c->status.track - 1];
            c->status.error[0]                   = 0;
        }
        else if (command >= AUDIO_TRANSPORT_CONTINUE && command <= AUDIO_TRANSPORT_PROGRAM)
        {
            if (command == AUDIO_TRANSPORT_PROGRAM && !c->playback.program_count)
            {
                return;
            }

            cd_playback_mode(&c->playback, command == AUDIO_TRANSPORT_PROGRAM ? CD_PROGRAM : command == AUDIO_TRANSPORT_SHUFFLE ? CD_SHUFFLE
                                                                                                                                : CD_CONTINUE,
                             c->status.tracks,
                             c->status.track);

            c->playback.repeat = command == AUDIO_TRANSPORT_REPEAT_ONE ? CD_REPEAT_ONE : command == AUDIO_TRANSPORT_REPEAT_ALL ? CD_REPEAT_ALL
                                                                                                                               : CD_REPEAT_OFF;

            if (command == AUDIO_TRANSPORT_PROGRAM)
            {
                c->status.track = c->playback.order[0];
                c->position     = c->toc.start[c->status.track - 1];
                c->restart      = 1;
            }

            c->tail_blocks = 0;
        }
        else
        {
            int next = cd_playback_next(&c->playback, c->status.track, command == AUDIO_TRANSPORT_NEXT ? 1 : -1, 0);

            if (next < 1)
            {
                next = 1;
            }

            if (next > c->status.tracks)
            {
                next = c->status.tracks;
            }

            c->status.track = next;
            c->position     = c->toc.start[next - 1];
        }

        /* Playback modes change the next boundary action, not the current stream. */

        if (command < AUDIO_TRANSPORT_CONTINUE)
        {
            c->restart = 1;
        }
    }
}

/**
 * @brief Advance held scanning and request resets only on entry or release.
 * @param c Controller state.
 * @param scan_direction Requested scan direction.
 * @param now Monotonic milliseconds.
 */
static void update_scan(CdController* c, int scan_direction, uint32_t now)
{
    if (c->scan_inhibited)
    {
        if (!scan_direction)
        {
            c->scan_inhibited = 0;
        }

        return;
    }

    if (!c->status.present || !c->status.tracks || !c->output.ready || c->status.error[0])
    {
        scan_direction = 0;
    }

    if (!c->status.present)
    {
        memset(&c->scan, 0, sizeof(c->scan));
    }

    int was_scanning = c->scan.direction;

    c->position        = cd_scan_step(&c->scan, scan_direction, now, c->position, &c->toc);
    c->status.scanning = c->scan.direction;

    if (was_scanning || c->scan.direction)
    {
        c->status.track = cd_track_at(&c->toc, c->position);

        /* Flush once on entry/release; scanning itself does no drive I/O. */

        if (!was_scanning || !c->scan.direction)
        {
            c->restart = 1;
        }
    }
}

/**
 * @brief Apply programs, transport commands, and held scanning in order.
 * @param c Controller state.
 * @param requests Copied worker requests.
 * @param now Monotonic milliseconds.
 */
static void apply_requests(CdController* c, const CdRequests* requests, uint32_t now)
{
    int new_program = requests->program_count && c->status.present && c->status.tracks &&
                      cd_playback_program(&c->playback, requests->program, requests->program_count, c->status.tracks) == 0;

    if (new_program)
    {
        memset(c->status.played, 0, sizeof(c->status.played));

        c->status.track = c->playback.order[0];
        c->position     = c->toc.start[c->status.track - 1];
        c->restart      = 1;
    }

    if (requests->have_command)
    {
        apply_command(c, requests->command, now);
    }

    update_scan(c, requests->scan_direction, now);
}

/**
 * @brief Apply a seek, pause, or stop by resetting drive, output, and analysis buffers.
 * @param c Controller state.
 */
static void reset_stream(CdController* c)
{
    c->restart = 0;

    int stopped = cd_output_stop(&c->output) == 0;

    if (platform_cd_drive_reset(&c->drive, c->position) != 0)
    {
        drive_unresponsive(c);
        return;
    }

    c->submitted   = c->position;
    c->tail_blocks = 0;

    int reset = cd_output_reset(&c->output, c->runtime, stopped && c->status.playing && !c->status.scanning) == 0;

    if (!stopped || !reset)
    {
        c->status.playing = c->status.paused = c->status.scanning = 0;

        memset(&c->scan, 0, sizeof(c->scan));
        snprintf(c->status.error, sizeof(c->status.error), "SOUND OUTPUT ERROR");
    }

    c->runtime->clear_audio();
}

/**
 * @brief Recheck media before treating a failed read as a playback fault.
 * @param c Controller state.
 * @param now Monotonic milliseconds.
 */
static void read_error(CdController* c, uint32_t now)
{
    unsigned generation = c->status.generation;

    /* Ejection can interrupt a read between scheduled media checks. */
    c->checked = now - MEDIA_POLL_MS;

    cd_controller_detect(c, now);

    if (c->status.present && c->status.generation == generation)
    {
        stream_error(c, "CD READ ERROR - PRESS PLAY TO RETRY");
    }
    else if (c->restart)
    {
        reset_stream(c);
    }
}

/**
 * @brief Feed one available sector and update read-ahead, position, and track boundaries.
 * @param c Controller state.
 * @param now Monotonic milliseconds for media rechecks after read failure.
 */
static void stream_step(CdController* c, uint32_t now)
{
    int            continuous = c->playback.mode == CD_CONTINUE && c->playback.repeat != CD_REPEAT_ONE;
    int            end        = c->toc.start[continuous ? c->toc.count : c->status.track];
    const uint8_t* sector     = platform_cd_drive_sector(&c->drive);

    /* A mode change may shorten the boundary after read-ahead; retain buffers but cap submission. */

    if (sector && c->submitted < end)
    {
        const uint8_t* samples = NULL;
        int            result  = cd_output_submit(&c->output, sector, &samples);

        if (result < 0)
        {
            stream_error(c, "SOUND OUTPUT ERROR");
            return;
        }

        if (result == 0)
        {
            c->runtime->pcm(samples, CD_OUTPUT_FRAMES);
            platform_cd_drive_consume(&c->drive);
            ++c->submitted;
        }
    }

    if (c->status.playing && platform_cd_drive_prefetch(&c->drive, end) != 0)
    {
        read_error(c, now);
        return;
    }

    int audio_bytes = 0;
    int queued      = cd_output_queued(&c->output, &audio_bytes);

    if (queued < 0)
    {
        stream_error(c, "SOUND OUTPUT ERROR");
        return;
    }

    c->position = c->submitted - (audio_bytes + CD_OUTPUT_BYTES - 1) / CD_OUTPUT_BYTES;

    if (c->position < c->toc.start[0])
    {
        c->position = c->toc.start[0];
    }

    if (c->playback.mode == CD_PROGRAM && c->position > c->toc.start[c->status.track - 1])
    {
        c->status.played[c->status.track - 1] = 1;
    }

    while (continuous && c->status.track < c->status.tracks && c->position >= c->toc.start[c->status.track])
    {
        ++c->status.track;
    }

    /* Feed silence on underrun and lead-out so audsrv cannot replay stale ring contents. */

    if (c->status.playing && (platform_cd_drive_waiting(&c->drive) || c->submitted >= end))
    {
        int result = cd_output_silence(&c->output, queued);

        if (result < 0)
        {
            stream_error(c, "SOUND OUTPUT ERROR");
            return;
        }

        if (result == 0 && c->submitted >= end)
        {
            ++c->tail_blocks;
        }
    }

    if (c->submitted >= end && c->tail_blocks >= OUTPUT_LEAD_OUT_BLOCKS)
    {
        if (cd_output_stop(&c->output) != 0)
        {
            stream_error(c, "SOUND OUTPUT ERROR");
            return;
        }

        int next = cd_playback_next(&c->playback, c->status.track, 1, 1);

        if (next)
        {
            c->status.track = next;
            c->position     = c->toc.start[next - 1];
            c->restart      = 1;
        }
        else
        {
            c->status.playing = c->status.paused = 0;
            c->position                          = end;
        }

        c->runtime->clear_audio();
    }
}

/**
 * @brief Calculate bounded playback progress within a sector range.
 * @param position Audible or requested sector position.
 * @param start First sector in the range.
 * @param end Exclusive end sector.
 * @return Fraction from zero to one; zero for an empty range.
 */
static float progress(int position, int start, int end)
{
    if (end <= start || position <= start)
    {
        return 0;
    }

    if (position >= end)
    {
        return 1;
    }

    return (float)(position - start) / (float)(end - start);
}

/**
 * @brief Derive display times, progress, and modes from current playback state.
 * @param c Controller state.
 */
static void update_status(CdController* c)
{
    c->status.track_progress        = 0;
    c->status.disc_progress         = 0;
    c->status.disc_duration_seconds = 0;

    if (c->status.present && c->status.tracks)
    {
        int start = c->toc.start[c->status.track - 1];

        c->status.elapsed_seconds       = c->position > start ? (unsigned)(c->position - start) / CD_SECTORS_PER_SECOND : 0;
        c->status.duration_seconds      = (c->toc.start[c->status.track] - start) / CD_SECTORS_PER_SECOND;
        c->status.track_progress        = progress(c->position, start, c->toc.start[c->status.track]);
        c->status.disc_progress         = progress(c->position, c->toc.start[0], c->toc.start[c->toc.count]);
        c->status.disc_duration_seconds = (c->toc.start[c->toc.count] - c->toc.start[0]) / CD_SECTORS_PER_SECOND;
    }

    memset(c->status.programmed, 0, sizeof(c->status.programmed));

    if (c->status.present && c->playback.mode == CD_PROGRAM)
    {
        for (int i = 0; i < c->playback.count; ++i)
        {
            c->status.programmed[c->playback.order[i] - 1] = 1;
        }
    }

    c->status.mode   = c->playback.mode;
    c->status.repeat = c->playback.repeat;
}

/**
 * @brief Forward platform drive startup progress through the CD worker.
 * @param context Controller receiving startup progress.
 * @param message Borrowed synchronous progress text.
 */
static void drive_stage(void* context, const char* message)
{
    CdController* c = context;

    c->runtime->stage(&c->status, message);
}

void cd_controller_open(CdController* c, const CdRuntime* runtime, uint32_t now, int autoplay)
{
    memset(c, 0, sizeof(*c));

    c->autoplay        = autoplay;
    c->runtime         = runtime;
    c->status.checking = 1;
    c->checked         = now - MEDIA_POLL_MS;

    Ps2CdDriveRuntime drive = { .running = runtime->running, .context = c, .stage = drive_stage, .error = c->status.error, .capacity = sizeof(c->status.error) };

    int result = platform_cd_drive_open(&c->drive, &drive);

    c->drive_ready             = result == 0;
    c->status.restart_required = result == PS2_CD_DRIVE_ERROR_RESTART_REQUIRED;

    if (!c->drive_ready)
    {
        c->status.checking = 0;

        STROOM_LOG("%s; using network audio", c->status.error);
    }
}

void cd_controller_step(CdController* c, const CdRequests* requests, uint32_t now)
{
    if (!c->drive_ready || platform_cd_drive_failed(&c->drive))
    {
        return;
    }

    if (c->status.present && requests->generation == c->status.generation)
    {
        apply_requests(c, requests, now);
    }

    if (c->restart)
    {
        reset_stream(c);
    }

    int read_result = platform_cd_drive_poll(&c->drive, now);

    if (platform_cd_drive_failed(&c->drive))
    {
        drive_unresponsive(c);
        update_status(c);
        return;
    }

    if (read_result < 0)
    {
        read_error(c, now);
        update_status(c);
        return;
    }

    if (c->status.present && c->status.playing && c->output.ready && !c->status.scanning)
    {
        stream_step(c, now);
    }

    update_status(c);
}

void cd_controller_close(CdController* c)
{
    platform_cd_drive_close(&c->drive);

    if (platform_cd_drive_failed(&c->drive))
    {
        c->status.restart_required = 1;

        snprintf(c->status.error, sizeof(c->status.error), "CD DRIVE UNRESPONSIVE - RESTART REQUIRED");
    }

    cd_output_close(&c->output);
}
