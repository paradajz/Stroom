#include "audio/cd/cd_controller.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>

static CdController c;
static int          controller_opened;
static volatile int running = 1;
static Ps2CdMedia   media;
static int          sound_failures;
static int          toc_failures;
static int          toc_invalid;
static int          drive_failure;
static int          read_failure;
static int          prefetch_failure;
static int          submit_failure;
static int          reset_failure;
static int          stop_failure;
static int          sound_attempts;
static int          toc_attempts;
static int          drive_resets;
static int          output_resets;
static int          output_stops;
static int          output_prepares;
static int          clears;
static int          captured;
static int          available_sectors;
static int          prefetches;
static int          last_end;
static int          queued;
static int          queued_audio;
static int          silence_failure;
static uint8_t      fixture_sector[CD_SECTOR_BYTES];
static uint8_t      fixture_samples[CD_OUTPUT_BYTES];

static void            stage(CdPlaybackStatus* status, const char* message);
static void            clear_audio(void);
static void            capture(const uint8_t* pcm, unsigned frames);
static const CdRuntime runtime = { &running, stage, clear_audio, capture };

/**
 * @brief Copy simulated progress text into controller status.
 * @param status Status destination.
 * @param message Progress text.
 */
static void stage(CdPlaybackStatus* status, const char* message)
{
    snprintf(status->status, sizeof(status->status), "%s", message);
}

/**
 * @brief Count analysis-buffer resets.
 */
static void clear_audio(void)
{
    ++clears;
}

/**
 * @brief Check that analysis receives the same PCM returned by sound output.
 * @param pcm Submitted PCM pointer.
 * @param frames Stereo frame count.
 */
static void capture(const uint8_t* pcm, unsigned frames)
{
    TEST_ASSERT_EQUAL_PTR(fixture_samples, pcm);
    TEST_ASSERT_EQUAL_INT(CD_OUTPUT_FRAMES, frames);
    ++captured;
}

/* Hardware substitutes implement the component contracts declared in their headers. */
int platform_cd_drive_open(Ps2CdDrive* d, const Ps2CdDriveRuntime* runtime)
{
    memset(d, 0, sizeof(*d));

    d->pending = -1;

    if (drive_failure)
    {
        snprintf(runtime->error, runtime->capacity, "DRIVE FAILURE");
    }

    return drive_failure;
}

Ps2CdMedia platform_cd_drive_media(void)
{
    return media;
}

int platform_cd_drive_toc(Ps2CdToc* toc)
{
    static uint8_t raw[60];

    memset(raw, 0, sizeof(raw));

    *toc = (Ps2CdToc){ .data = raw, .size = sizeof(raw) };

    ++toc_attempts;

    if (toc_failures)
    {
        --toc_failures;

        toc->error = 1;

        return -1;
    }

    if (toc_invalid)
    {
        --toc_invalid;
        return 0;
    }

    raw[CD_TOC_POINT_OFFSET]                            = CD_TOC_FIRST_TRACK;
    raw[CD_TOC_MSF_OFFSET]                              = 1;
    raw[CD_TOC_ENTRY_BYTES + CD_TOC_POINT_OFFSET]       = CD_TOC_LAST_TRACK;
    raw[CD_TOC_ENTRY_BYTES + CD_TOC_MSF_OFFSET]         = 3;
    raw[2 * CD_TOC_ENTRY_BYTES + CD_TOC_POINT_OFFSET]   = CD_TOC_LEAD_OUT;
    raw[2 * CD_TOC_ENTRY_BYTES + CD_TOC_MSF_OFFSET + 1] = 8;

    for (unsigned i = 0; i < 3; ++i)
    {
        unsigned at                     = (CD_TOC_HEADER_ENTRIES + i) * CD_TOC_ENTRY_BYTES;
        raw[at + CD_TOC_POINT_OFFSET]   = i + 1;
        raw[at + CD_TOC_MSF_OFFSET + 1] = (i + 1) * 2;
    }

    return 0;
}

int platform_cd_drive_failed(const Ps2CdDrive* d)
{
    return d->failed;
}

int platform_cd_drive_idle(const Ps2CdDrive* d)
{
    return d->pending < 0;
}

void platform_cd_drive_close(Ps2CdDrive* d)
{
    if (!d->failed)
    {
        d->pending = -1;
    }
}

int platform_cd_drive_reset(Ps2CdDrive* d, int position)
{
    if (d->failed)
    {
        return -1;
    }

    ++drive_resets;
    platform_cd_drive_close(d);

    d->cursor         = position;
    available_sectors = 0;

    return 0;
}

int platform_cd_drive_poll(Ps2CdDrive* d, uint32_t now)
{
    (void)now;

    if (read_failure == 2)
    {
        d->failed = 1;

        return -1;
    }

    int success = !read_failure;

    read_failure = 0;

    return success ? 0 : -1;
}

const uint8_t* platform_cd_drive_sector(Ps2CdDrive* d)
{
    (void)d;

    return available_sectors ? fixture_sector : NULL;
}

void platform_cd_drive_consume(Ps2CdDrive* d)
{
    (void)d;
    TEST_ASSERT_GREATER_THAN_INT(0, available_sectors);
    --available_sectors;
}

int platform_cd_drive_prefetch(Ps2CdDrive* d, int end)
{
    (void)d;
    ++prefetches;

    last_end = end;

    return (!prefetch_failure) ? 0 : -1;
}

int platform_cd_drive_waiting(const Ps2CdDrive* d)
{
    (void)d;

    return !available_sectors;
}

int cd_output_open(CdOutput* o, const CdRuntime* runtime, CdPlaybackStatus* status)
{
    (void)runtime;

    if (!o->ready)
    {
        ++sound_attempts;

        if (sound_failures)
        {
            --sound_failures;
            strcpy(status->error, "SOUND INIT ERROR");
            return -1;
        }

        o->ready = 1;
    }

    return 0;
}

int cd_output_stop(const CdOutput* o)
{
    if (o->ready)
    {
        ++output_stops;
        return (!stop_failure) ? 0 : -1;
    }

    return 0;
}

int cd_output_reset(CdOutput* o, const CdRuntime* runtime, int resume)
{
    (void)runtime;
    ++output_resets;

    if (o->ready && resume)
    {
        ++output_prepares;
        return (!reset_failure) ? 0 : -1;
    }

    return 0;
}

int cd_output_submit(CdOutput* o, const uint8_t* sector, const uint8_t** samples)
{
    TEST_ASSERT_TRUE(o->ready);
    TEST_ASSERT_EQUAL_PTR(fixture_sector, sector);

    if (submit_failure)
    {
        return -1;
    }

    *samples = fixture_samples;

    return 0;
}

int cd_output_queued(const CdOutput* o, int* audio_bytes)
{
    (void)o;

    *audio_bytes = queued_audio;

    return queued;
}

int cd_output_silence(CdOutput* o, int queued)
{
    (void)o;

    return silence_failure ? -1 : queued <= 4096 ? 0
                                                 : 1;
}

void cd_output_close(CdOutput* o)
{
    cd_output_stop(o);

    o->ready = 0;
}

/**
 * @brief Reset hardware fixtures and observable effects between scenarios.
 */
static void fixture(void)
{
    prefetch_failure = 0;
    queued_audio     = 0;
    silence_failure  = 0;
    stop_failure     = 0;
    media            = (Ps2CdMedia){ .audio = 1 };
    toc_invalid      = 0;
    sound_failures = toc_failures = drive_failure = read_failure = submit_failure = reset_failure = 0;
    sound_attempts = toc_attempts = drive_resets = output_resets = output_stops = output_prepares = 0;
    clears = captured = available_sectors = prefetches = last_end = queued = 0;
}

/**
 * @brief Close the fixture controller, including after a failed assertion.
 */
static void close_controller(void)
{
    if (controller_opened)
    {
        controller_opened = 0;

        cd_controller_close(&c);
    }
}

/**
 * @brief Initialize the fixture controller and register it for cleanup.
 */
static void open_controller(void)
{
    controller_opened = 1;

    cd_controller_open(&c, &runtime, 0, 1);
}

/**
 * @brief Reset controller storage and hardware substitutes before each case.
 */
void setUp(void)
{
    TEST_ASSERT_FALSE_MESSAGE(controller_opened, "Previous case did not close its controller");
    memset(&c, 0, sizeof(c));
    memset(fixture_sector, 0, sizeof(fixture_sector));
    memset(fixture_samples, 0, sizeof(fixture_samples));

    controller_opened = 0;
    running           = 1;

    fixture();
}

/**
 * @brief Release simulated controller resources even when a case fails.
 */
void tearDown(void)
{
    close_controller();
}

/**
 * @brief Perform a worker iteration with media detection before request processing.
 * @param c Controller state.
 * @param requests Requested actions.
 * @param now Monotonic milliseconds.
 */
static void tick(CdController* c, CdRequests requests, uint32_t now)
{
    requests.generation = c->status.generation;

    cd_controller_detect(c, now);
    cd_controller_step(c, &requests, now);
}

/**
 * @brief Send a transport command in one worker iteration.
 * @param c Controller state.
 * @param command Requested command.
 * @param now Monotonic milliseconds.
 */
static void command(CdController* c, AudioTransportCommand command, uint32_t now)
{
    tick(c, (CdRequests){ .have_command = 1, .command = command }, now);
}

/**
 * @brief Start a fixture disc and begin playback.
 */
static void playing(void)
{
    fixture();
    open_controller();
    tick(&c, (CdRequests){ 0 }, 0);
    TEST_ASSERT_TRUE(c.status.present);
    TEST_ASSERT_EQUAL_INT(3, c.status.tracks);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_EQUAL_INT(1, output_prepares);
}

/**
 * @brief Verify repeat/shuffle/continue do not stop sound or discard queued fixture_samples.
 */
static void seamless_modes(void)
{
    playing();

    int                   stops = output_stops, resets = drive_resets, cleared = clears;
    AudioTransportCommand modes[] = { AUDIO_TRANSPORT_REPEAT_ONE, AUDIO_TRANSPORT_SHUFFLE, AUDIO_TRANSPORT_CONTINUE };

    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
    {
        available_sectors = 1;

        command(&c, modes[i], 2 + i);
        TEST_ASSERT_TRUE(c.status.playing);
        TEST_ASSERT_EQUAL_INT((int)i + 1, captured);
        TEST_ASSERT_EQUAL_INT(stops, output_stops);
        TEST_ASSERT_EQUAL_INT(resets, drive_resets);
        TEST_ASSERT_EQUAL_INT(cleared, clears);
        TEST_ASSERT_EQUAL_INT(1, output_prepares);
    }

    TEST_ASSERT_EQUAL_INT(CD_CONTINUE, c.status.mode);
    TEST_ASSERT_EQUAL_INT(CD_REPEAT_OFF, c.status.repeat);
    close_controller();
}

/**
 * @brief Verify sound startup can fail repeatedly, then recover with the same Play request.
 */
static void sound_retry(void)
{
    fixture();

    sound_failures = 2;

    open_controller();
    tick(&c, (CdRequests){ 0 }, 0);
    TEST_ASSERT_EQUAL_INT(3, c.status.tracks);
    TEST_ASSERT_FALSE(c.output.ready);
    TEST_ASSERT_TRUE(c.status.error[0]);
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 1);
    TEST_ASSERT_EQUAL_INT(2, sound_attempts);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_TRUE(c.status.error[0]);
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 2);
    TEST_ASSERT_EQUAL_INT(3, sound_attempts);
    TEST_ASSERT_EQUAL_INT(1, toc_attempts);
    TEST_ASSERT_TRUE(c.output.ready);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_FALSE(c.status.error[0]);
    TEST_ASSERT_EQUAL_INT(1, output_prepares);
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 3);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_TRUE(c.status.paused);
    TEST_ASSERT_EQUAL_INT(3, sound_attempts);
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 4);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_FALSE(c.status.paused);
    TEST_ASSERT_EQUAL_INT(3, sound_attempts);
    close_controller();
}

/**
 * @brief Verify TOC retries remain bounded and Play explicitly restarts them.
 */
static void toc_retry(void)
{
    fixture();

    toc_failures = 21;

    open_controller();

    for (unsigned now = 0; now < 10000; now += 500)
    {
        tick(&c, (CdRequests){ 0 }, now);
        TEST_ASSERT_FALSE(c.status.error[0]);
    }

    tick(&c, (CdRequests){ 0 }, 10000);
    TEST_ASSERT_FALSE(c.status.tracks);
    TEST_ASSERT_TRUE(c.status.error[0]);
    TEST_ASSERT_EQUAL_INT(21, toc_attempts);
    tick(&c, (CdRequests){ 0 }, 10500);
    TEST_ASSERT_EQUAL_INT(21, toc_attempts);
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 10501);
    TEST_ASSERT_FALSE(c.status.error[0]);
    tick(&c, (CdRequests){ 0 }, 10502);
    TEST_ASSERT_EQUAL_INT(22, toc_attempts);
    TEST_ASSERT_EQUAL_INT(3, c.status.tracks);
    TEST_ASSERT_TRUE(c.output.ready);
    close_controller();
}

/**
 * @brief Verify track changes, programs, and held scanning reset only at discontinuities.
 */
static void navigation(void)
{
    playing();
    command(&c, AUDIO_TRANSPORT_NEXT, 2);
    TEST_ASSERT_EQUAL_INT(2, c.status.track);
    TEST_ASSERT_EQUAL_INT(150, c.submitted);
    TEST_ASSERT_TRUE(c.status.playing);
    command(&c, AUDIO_TRANSPORT_PREVIOUS, 3);
    TEST_ASSERT_EQUAL_INT(1, c.status.track);
    TEST_ASSERT_EQUAL_INT(0, c.submitted);
    command(&c, AUDIO_TRANSPORT_NEXT, 4);
    command(&c, AUDIO_TRANSPORT_NEXT, 4);
    TEST_ASSERT_EQUAL_INT(3, c.status.track);
    TEST_ASSERT_EQUAL_INT(300, c.submitted);
    tick(&c, (CdRequests){ .program = { 2, 1 }, .program_count = 2 }, 5);
    TEST_ASSERT_EQUAL_INT(CD_PROGRAM, c.status.mode);
    TEST_ASSERT_EQUAL_INT(2, c.status.track);
    TEST_ASSERT_EQUAL_INT(150, c.submitted);

    int reads = prefetches, resets = drive_resets;

    tick(&c, (CdRequests){ .scan_direction = 1 }, 6);
    TEST_ASSERT_EQUAL_INT(1, c.status.scanning);
    TEST_ASSERT_EQUAL_INT(resets + 1, drive_resets);
    TEST_ASSERT_EQUAL_INT(reads, prefetches);
    tick(&c, (CdRequests){ .scan_direction = 1 }, 106);
    TEST_ASSERT_EQUAL_INT(210, c.position);
    TEST_ASSERT_EQUAL_INT(resets + 1, drive_resets);
    TEST_ASSERT_EQUAL_INT(reads, prefetches);
    tick(&c, (CdRequests){ 0 }, 107);
    TEST_ASSERT_FALSE(c.status.scanning);
    TEST_ASSERT_EQUAL_INT(resets + 2, drive_resets);
    TEST_ASSERT_EQUAL_INT(reads + 1, prefetches);
    command(&c, AUDIO_TRANSPORT_STOP, 108);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_FALSE(c.status.paused);
    TEST_ASSERT_EQUAL_INT(150, c.position);
    close_controller();
}

/**
 * @brief Verify failed Stop and Pause report errors, stop feeding, and allow Play to retry.
 */
static void stop_errors(void)
{
    const AudioTransportCommand commands[] = { AUDIO_TRANSPORT_STOP, AUDIO_TRANSPORT_PLAY_PAUSE };

    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
    {
        playing();

        int reads   = prefetches;
        int cleared = clears;
        int resets  = drive_resets;

        stop_failure = 1;

        command(&c, commands[i], 2);
        TEST_ASSERT_EQUAL_STRING("SOUND OUTPUT ERROR", c.status.error);
        TEST_ASSERT_FALSE(c.status.playing);
        TEST_ASSERT_FALSE(c.status.paused);
        TEST_ASSERT_FALSE(c.status.scanning);
        TEST_ASSERT_FALSE(c.restart);
        TEST_ASSERT_EQUAL_INT(resets + 1, drive_resets);
        TEST_ASSERT_EQUAL_INT(cleared + 1, clears);
        TEST_ASSERT_EQUAL_INT(1, output_prepares);
        tick(&c, (CdRequests){ 0 }, 3);
        TEST_ASSERT_EQUAL_INT(reads, prefetches);

        stop_failure = 0;

        command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 4);
        TEST_ASSERT_TRUE(c.status.playing);
        TEST_ASSERT_EQUAL_STRING("", c.status.error);
        TEST_ASSERT_EQUAL_INT(2, output_prepares);
        close_controller();
    }
}

/**
 * @brief Verify a failed boundary stop cannot automatically restart repeat playback.
 */
static void boundary_stop_error(void)
{
    playing();

    c.playback.repeat = CD_REPEAT_ONE;
    c.submitted = c.position = 150;
    c.tail_blocks            = 7;
    stop_failure             = 1;

    tick(&c, (CdRequests){ 0 }, 2);
    TEST_ASSERT_EQUAL_STRING("SOUND OUTPUT ERROR", c.status.error);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_FALSE(c.restart);
    tick(&c, (CdRequests){ 0 }, 3);
    TEST_ASSERT_EQUAL_INT(1, output_prepares);
}

/**
 * @brief Verify lead-out drain, repeat-one boundaries, and continuous track progression.
 */
static void boundaries(void)
{
    playing();

    c.submitted = c.position = 149;
    available_sectors        = 1;

    tick(&c, (CdRequests){ 0 }, 2);
    TEST_ASSERT_EQUAL_INT(2, c.status.track);
    TEST_ASSERT_EQUAL_INT(150, c.submitted);
    TEST_ASSERT_EQUAL_INT(450, last_end);
    command(&c, AUDIO_TRANSPORT_PREVIOUS, 3);

    c.playback.repeat = CD_REPEAT_ONE;
    c.submitted = c.position = 149;
    available_sectors        = 2; /* Simulate read-ahead beyond the new repeat boundary. */

    tick(&c, (CdRequests){ 0 }, 4);
    TEST_ASSERT_EQUAL_INT(1, c.status.track);
    TEST_ASSERT_EQUAL_INT(150, c.submitted);
    TEST_ASSERT_EQUAL_INT(2, captured);
    TEST_ASSERT_EQUAL_INT(150, last_end);

    for (unsigned now = 5; now <= 11; ++now)
    {
        tick(&c, (CdRequests){ 0 }, now);
    }

    TEST_ASSERT_TRUE(c.restart);
    TEST_ASSERT_EQUAL_INT(0, c.position);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_EQUAL_INT(2, captured);
    tick(&c, (CdRequests){ 0 }, 12);
    TEST_ASSERT_FALSE(c.restart);
    TEST_ASSERT_EQUAL_INT(0, c.submitted);

    c.playback.repeat = CD_REPEAT_OFF;
    c.status.track    = 3;
    c.submitted = c.position = 450;

    for (unsigned now = 13; now <= 20; ++now)
    {
        tick(&c, (CdRequests){ 0 }, now);
    }

    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_FALSE(c.status.paused);
    TEST_ASSERT_FALSE(c.restart);
    close_controller();
}

/**
 * @brief Natural completion retains progress until Play, Stop, or a track skip.
 */
static void completed_progress(void)
{
    for (int program = 0; program <= 1; ++program)
    {
        for (int action = 0; action < 3; ++action)
        {
            playing();

            if (program)
            {
                tick(&c, (CdRequests){ .program = { 1 }, .program_count = 1 }, 2);
            }
            else
            {
                command(&c, AUDIO_TRANSPORT_NEXT, 2);
                command(&c, AUDIO_TRANSPORT_NEXT, 2);
            }

            int track = program ? 1 : 3;

            c.submitted = c.position = c.toc.start[track];

            for (unsigned now = 3; now <= 10; ++now)
            {
                tick(&c, (CdRequests){ 0 }, now);
            }

            TEST_ASSERT_FALSE(c.status.playing);
            TEST_ASSERT_FALSE(c.status.paused);
            TEST_ASSERT_EQUAL_INT(c.toc.start[track], c.position);
            TEST_ASSERT_EQUAL_FLOAT(1, c.status.track_progress);
            TEST_ASSERT_FLOAT_WITHIN(0.00001f, program ? 1.0f / 3 : 1, c.status.disc_progress);
            tick(&c, (CdRequests){ 0 }, 11);
            TEST_ASSERT_EQUAL_FLOAT(1, c.status.track_progress);

            if (action == 2)
            {
                command(&c, AUDIO_TRANSPORT_PREVIOUS, 12);
                command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 12);
                TEST_ASSERT_EQUAL_INT(program ? 1 : 2, c.status.track);
            }
            else
            {
                command(&c, action ? AUDIO_TRANSPORT_STOP : AUDIO_TRANSPORT_PLAY_PAUSE, 12);
            }

            TEST_ASSERT_EQUAL_INT(action != 1, c.status.playing);
            TEST_ASSERT_EQUAL_INT(c.toc.start[c.status.track - 1], c.position);
            TEST_ASSERT_EQUAL_FLOAT(0, c.status.track_progress);
            close_controller();
        }
    }
}

/**
 * @brief Verify I/O failures stop playback, Play recovers, and removal cancels reads.
 */
static void failures_and_removal(void)
{
    playing();

    read_failure = 1;

    tick(&c, (CdRequests){ 0 }, 2);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_NOT_NULL(strstr(c.status.error, "CD READ ERROR"));
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 3);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_FALSE(c.status.error[0]);

    available_sectors = 1;
    submit_failure    = 1;

    tick(&c, (CdRequests){ 0 }, 4);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_NOT_NULL(strstr(c.status.error, "SOUND OUTPUT ERROR"));
    TEST_ASSERT_FALSE(captured);

    submit_failure = 0;
    reset_failure  = 1;

    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 5);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_TRUE(c.status.error[0]);

    reset_failure = 0;

    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 6);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_FALSE(c.status.error[0]);

    c.drive.pending = 1;
    media           = (Ps2CdMedia){ .changed = 1, .absent = 1 };

    int resets = drive_resets;

    tick(&c, (CdRequests){ 0 }, 502);
    TEST_ASSERT_FALSE(c.status.present);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_EQUAL_INT(-1, c.drive.pending);
    TEST_ASSERT_EQUAL_INT(resets + 1, drive_resets);

    media = (Ps2CdMedia){ .audio = 1 };

    tick(&c, (CdRequests){ 0 }, 1002);
    TEST_ASSERT_TRUE(c.status.present);
    TEST_ASSERT_EQUAL_INT(2, c.status.generation);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_EQUAL_INT(1, sound_attempts); /* The initialized sound service survives media changes. */
    close_controller();
    fixture();

    drive_failure = PS2_CD_DRIVE_ERROR_INITIALIZE;

    open_controller();
    tick(&c, (CdRequests){ 0 }, 0);
    TEST_ASSERT_FALSE(c.drive_ready);
    TEST_ASSERT_FALSE(c.status.restart_required);
    TEST_ASSERT_FALSE(c.status.checking);
    TEST_ASSERT_TRUE(c.status.error[0]);
    TEST_ASSERT_FALSE(toc_attempts);
    close_controller();
}

/**
 * @brief Verify queue and silence failures preserve the current track and stop playback.
 */
static void output_queue_failures(void)
{
    playing();

    c.submitted = 150;
    c.position  = 149;
    queued      = -1;

    tick(&c, (CdRequests){ 0 }, 2);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_EQUAL_INT(1, c.status.track);
    TEST_ASSERT_EQUAL_INT(149, c.position);
    TEST_ASSERT_NOT_NULL(strstr(c.status.error, "SOUND OUTPUT ERROR"));

    queued = 0;

    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 3);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_FALSE(c.status.error[0]);

    c.playback.mode     = CD_PROGRAM;
    c.playback.count    = 2;
    c.playback.order[0] = 1;
    c.playback.order[1] = 2;
    c.submitted = c.position = 150;
    c.tail_blocks            = 7;
    silence_failure          = 1;

    tick(&c, (CdRequests){ 0 }, 4);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_EQUAL_INT(1, c.status.track);
    TEST_ASSERT_FALSE(c.restart);
    TEST_ASSERT_EQUAL_INT(7, c.tail_blocks);
    TEST_ASSERT_NOT_NULL(strstr(c.status.error, "SOUND OUTPUT ERROR"));
    close_controller();
}

/**
 * @brief Preserve the permanent startup result in the controller status.
 */
static void restart_required_at_startup(void)
{
    drive_failure = PS2_CD_DRIVE_ERROR_RESTART_REQUIRED;

    open_controller();
    TEST_ASSERT_FALSE(c.drive_ready);
    TEST_ASSERT_TRUE(c.status.restart_required);
    TEST_ASSERT_FALSE(c.status.checking);
    close_controller();
    TEST_ASSERT_TRUE(c.status.restart_required);
}

/** @brief Fatal reads stop sound and preserve restart status across media changes. */
static void unresponsive_drive(void)
{
    playing();

    c.drive.pending = 1;

    int cleared = clears;

    read_failure = 2;

    tick(&c, (CdRequests){ 0 }, 2);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_FALSE(c.status.paused);
    TEST_ASSERT_FALSE(c.status.scanning);
    TEST_ASSERT_TRUE(c.status.present);
    TEST_ASSERT_EQUAL_INT(cleared + 1, clears);
    TEST_ASSERT_EQUAL_STRING("CD DRIVE UNRESPONSIVE - RESTART REQUIRED", c.status.error);
    TEST_ASSERT_TRUE(c.status.restart_required);

    int resets = drive_resets, prepared = output_prepares, toc_reads = toc_attempts;

    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 3);
    tick(&c, (CdRequests){ .scan_direction = 1 }, 4);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_EQUAL_INT(1, c.drive.pending);
    TEST_ASSERT_EQUAL_INT(1, c.status.track);
    TEST_ASSERT_EQUAL_INT(resets, drive_resets);
    TEST_ASSERT_EQUAL_INT(prepared, output_prepares);

    media = (Ps2CdMedia){ .changed = 1, .absent = 1 };

    tick(&c, (CdRequests){ 0 }, 500);
    TEST_ASSERT_FALSE(c.status.present);
    TEST_ASSERT_NOT_NULL(strstr(c.status.error, "RESTART REQUIRED"));
    TEST_ASSERT_TRUE(c.status.restart_required);

    media = (Ps2CdMedia){ .audio = 1 };

    tick(&c, (CdRequests){ 0 }, 1000);
    TEST_ASSERT_TRUE(c.status.present);
    TEST_ASSERT_FALSE(c.status.playing);
    TEST_ASSERT_EQUAL_INT(toc_reads, toc_attempts);
    TEST_ASSERT_NOT_NULL(strstr(c.status.error, "RESTART REQUIRED"));
    TEST_ASSERT_TRUE(c.status.restart_required);
    close_controller();
    TEST_ASSERT_EQUAL_INT(1, c.drive.pending);
}

/**
 * @brief Discard old-disc scans, commands, selections, and programs on replacement.
 */
static void replacement_requests(void)
{
    playing();
    tick(&c, (CdRequests){ .scan_direction = 1 }, 2);

    unsigned old_generation = c.status.generation;

    media = (Ps2CdMedia){ .changed = 1, .audio = 1 };

    tick(&c, (CdRequests){ .have_command = 1, .command = AUDIO_TRANSPORT_NEXT, .program = { 2, 3 }, .program_count = 2, .scan_direction = 1 }, 500);
    TEST_ASSERT_TRUE(c.status.generation != old_generation);
    TEST_ASSERT_EQUAL_INT(1, c.status.track);
    TEST_ASSERT_EQUAL_INT(0, c.position);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_FALSE(c.scan.direction);
    TEST_ASSERT_FALSE(c.status.scanning);
    TEST_ASSERT_EQUAL_INT(CD_CONTINUE, c.status.mode);

    media = (Ps2CdMedia){ .audio = 1 };

    CdRequests stale = { .generation = old_generation, .have_command = 1, .command = AUDIO_TRANSPORT_PLAY_PAUSE, .program = { 2, 3 }, .program_count = 2, .scan_direction = 1 };

    cd_controller_step(&c, &stale, 501);
    TEST_ASSERT_EQUAL_INT(1, c.status.track);
    TEST_ASSERT_EQUAL_INT(0, c.position);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_FALSE(c.scan.direction);
    TEST_ASSERT_EQUAL_INT(CD_CONTINUE, c.status.mode);
    tick(&c, (CdRequests){ 0 }, 502);
    TEST_ASSERT_TRUE(c.status.playing);
    tick(&c, (CdRequests){ .scan_direction = 1 }, 503);

    media = (Ps2CdMedia){ .absent = 1, .changed = 1 };

    tick(&c, (CdRequests){ .scan_direction = 1 }, 1000);
    TEST_ASSERT_FALSE(c.status.present);
    TEST_ASSERT_FALSE(c.scan.direction);
    TEST_ASSERT_FALSE(c.status.scanning);

    media = (Ps2CdMedia){ .audio = 1 };

    tick(&c, stale, 1500);
    TEST_ASSERT_EQUAL_INT(1, c.status.track);
    TEST_ASSERT_EQUAL_INT(0, c.position);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_FALSE(c.scan.direction);
    TEST_ASSERT_EQUAL_INT(CD_CONTINUE, c.status.mode);
    close_controller();
}

/**
 * @brief Play track three, replace the disc, and start the new disc at track one.
 */
static void replacement_playback(void)
{
    for (int observe_ejection = 0; observe_ejection < 2; ++observe_ejection)
    {
        playing();
        command(&c, AUDIO_TRANSPORT_NEXT, 2);
        command(&c, AUDIO_TRANSPORT_NEXT, 2);

        available_sectors = 1;

        tick(&c, (CdRequests){ 0 }, 3);
        TEST_ASSERT_TRUE(c.status.playing);
        TEST_ASSERT_EQUAL_INT(3, c.status.track);
        TEST_ASSERT_EQUAL_INT(301, c.position);

        unsigned old_generation = c.status.generation;

        if (observe_ejection)
        {
            media = (Ps2CdMedia){ .changed = 1, .absent = 1 };

            tick(&c, (CdRequests){ 0 }, 500);
            TEST_ASSERT_FALSE(c.status.present);
            TEST_ASSERT_FALSE(c.status.playing);
        }

        media = (Ps2CdMedia){ .changed = 1, .audio = 1 };

        tick(&c, (CdRequests){ 0 }, 1000);
        TEST_ASSERT_TRUE(c.status.generation != old_generation);
        TEST_ASSERT_TRUE(c.status.present);
        TEST_ASSERT_EQUAL_INT(1, c.status.track);
        TEST_ASSERT_TRUE(c.status.playing);
        TEST_ASSERT_EQUAL_INT(0, c.position);
        TEST_ASSERT_EQUAL_INT(0, c.submitted);
        TEST_ASSERT_EQUAL_INT(0, c.drive.cursor);

        media = (Ps2CdMedia){ .audio = 1 };

        tick(&c, (CdRequests){ 0 }, 1001);
        TEST_ASSERT_TRUE(c.status.playing);
        TEST_ASSERT_FALSE(c.status.paused);
        TEST_ASSERT_FALSE(c.status.error[0]);
        TEST_ASSERT_EQUAL_INT(1, c.status.track);
        TEST_ASSERT_EQUAL_INT(0, c.position);
        TEST_ASSERT_EQUAL_INT(0, c.submitted);

        available_sectors = 1;

        tick(&c, (CdRequests){ 0 }, 1002);
        TEST_ASSERT_EQUAL_INT(1, c.status.track);
        TEST_ASSERT_EQUAL_INT(1, c.position);
        TEST_ASSERT_EQUAL_INT(1, c.submitted);
        close_controller();
    }
}

/**
 * @brief Underrun silence must not rewind the clock or the pause/resume seek.
 */
static void underrun_position(void)
{
    playing();

    c.submitted = c.position = 75;

    tick(&c, (CdRequests){ 0 }, 2);

    queued = 2048;

    tick(&c, (CdRequests){ 0 }, 3);
    TEST_ASSERT_EQUAL_INT(75, c.position);
    TEST_ASSERT_EQUAL_INT(1, c.status.elapsed_seconds);
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 4);
    TEST_ASSERT_TRUE(c.status.paused);
    TEST_ASSERT_EQUAL_INT(75, c.position);
    TEST_ASSERT_EQUAL_INT(75, c.drive.cursor);
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 5);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_EQUAL_INT(75, c.position);
    TEST_ASSERT_EQUAL_INT(75, c.submitted);

    available_sectors = 1;
    queued            = 2048 + CD_OUTPUT_BYTES;
    queued_audio      = CD_OUTPUT_BYTES;

    tick(&c, (CdRequests){ 0 }, 6);
    TEST_ASSERT_EQUAL_INT(76, c.submitted);
    TEST_ASSERT_EQUAL_INT(75, c.position);

    queued       = 2048;
    queued_audio = 0;

    tick(&c, (CdRequests){ 0 }, 7);
    TEST_ASSERT_EQUAL_INT(76, c.position);
    TEST_ASSERT_EQUAL_INT(1, c.status.elapsed_seconds);
    close_controller();
}

/**
 * @brief Progress uses sector precision and excludes the lead-in from the disc span.
 */
static void playback_progress(void)
{
    playing();
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 2);

    c.toc          = (CdToc){ .count = 3, .start = { 150, 300, 600, 1050 } };
    c.status.track = 2;
    c.position     = 375;

    tick(&c, (CdRequests){ 0 }, 3);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, c.status.track_progress);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, c.status.disc_progress);
    TEST_ASSERT_EQUAL_UINT(12, c.status.disc_duration_seconds);

    c.position = 376;

    tick(&c, (CdRequests){ 0 }, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 76.0f / 300, c.status.track_progress);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 226.0f / 900, c.status.disc_progress);

    c.status.track = 1;
    c.position     = 150;

    tick(&c, (CdRequests){ 0 }, 5);
    TEST_ASSERT_EQUAL_FLOAT(0, c.status.track_progress);
    TEST_ASSERT_EQUAL_FLOAT(0, c.status.disc_progress);

    c.status.track = 3;
    c.position     = 1050;

    tick(&c, (CdRequests){ 0 }, 6);
    TEST_ASSERT_EQUAL_FLOAT(1, c.status.track_progress);
    TEST_ASSERT_EQUAL_FLOAT(1, c.status.disc_progress);
    TEST_ASSERT_EQUAL_UINT(12, c.status.disc_duration_seconds);

    media = (Ps2CdMedia){ .changed = 1, .absent = 1 };

    tick(&c, (CdRequests){ 0 }, 500);
    TEST_ASSERT_EQUAL_FLOAT(0, c.status.track_progress);
    TEST_ASSERT_EQUAL_FLOAT(0, c.status.disc_progress);
    TEST_ASSERT_EQUAL_UINT(0, c.status.disc_duration_seconds);
}

/**
 * @brief Stop cancels either scan direction until release and preserves the selected track.
 */
static void stop_scanning(void)
{
    for (int direction = -1; direction <= 1; direction += 2)
    {
        playing();
        command(&c, AUDIO_TRANSPORT_NEXT, 2);

        c.position = 200;

        tick(&c, (CdRequests){ .scan_direction = direction }, 3);
        tick(&c, (CdRequests){ .scan_direction = direction }, 4);
        tick(&c, (CdRequests){ .have_command = 1, .command = AUDIO_TRANSPORT_STOP, .scan_direction = direction }, 5);
        TEST_ASSERT_FALSE(c.status.playing);
        TEST_ASSERT_FALSE(c.status.scanning);
        TEST_ASSERT_FALSE(c.scan.direction);
        TEST_ASSERT_EQUAL_INT(2, c.status.track);
        TEST_ASSERT_EQUAL_INT(150, c.position);
        TEST_ASSERT_TRUE(c.scan_inhibited);
        tick(&c, (CdRequests){ .scan_direction = direction }, 100);
        TEST_ASSERT_EQUAL_INT(2, c.status.track);
        TEST_ASSERT_EQUAL_INT(150, c.position);
        TEST_ASSERT_FALSE(c.status.scanning);
        tick(&c, (CdRequests){ 0 }, 101);
        TEST_ASSERT_FALSE(c.scan_inhibited);
        TEST_ASSERT_EQUAL_INT(150, c.position);
        command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 102);
        TEST_ASSERT_TRUE(c.status.playing);
        TEST_ASSERT_EQUAL_INT(2, c.status.track);
        TEST_ASSERT_EQUAL_INT(150, c.position);
        tick(&c, (CdRequests){ .scan_direction = direction }, 103);
        tick(&c, (CdRequests){ .scan_direction = direction }, 113);
        TEST_ASSERT_EQUAL_INT(direction, c.status.scanning);
        TEST_ASSERT_EQUAL_INT(150 + direction * 6, c.position);
        close_controller();
    }
}

/** Verify insertion can recover from early drive failures and incomplete TOCs. */
static void lid_close_toc_recovery(void)
{
    open_controller();

    media = (Ps2CdMedia){ .absent = 1 };

    tick(&c, (CdRequests){ 0 }, 0);
    TEST_ASSERT_FALSE(c.status.present);

    media = (Ps2CdMedia){ .checking = 1 };

    tick(&c, (CdRequests){ 0 }, 500);
    TEST_ASSERT_TRUE(c.status.checking);

    media        = (Ps2CdMedia){ .audio = 1, .changed = 1 };
    toc_failures = 4;
    toc_invalid  = 2;

    tick(&c, (CdRequests){ 0 }, 1000);

    media.changed = 0;

    unsigned generation = c.status.generation;

    for (unsigned now = 1500; now <= 3500; now += 500)
    {
        tick(&c, (CdRequests){ 0 }, now);
        TEST_ASSERT_FALSE(c.status.error[0]);
        TEST_ASSERT_FALSE(c.status.tracks);
    }

    tick(&c, (CdRequests){ 0 }, 4000);
    TEST_ASSERT_EQUAL_INT(7, toc_attempts);
    TEST_ASSERT_EQUAL_INT(3, c.status.tracks);
    TEST_ASSERT_EQUAL_UINT(generation, c.status.generation);
    TEST_ASSERT_TRUE(c.output.ready);
    TEST_ASSERT_TRUE(c.status.playing);
}

/** Verify lid reopening cancels retries and reinsertion gets a fresh window. */
static void lid_reopen_during_toc(void)
{
    open_controller();

    toc_failures = 100;

    tick(&c, (CdRequests){ 0 }, 0);

    unsigned generation = c.status.generation;

    media = (Ps2CdMedia){ .absent = 1, .changed = 1 };

    tick(&c, (CdRequests){ 0 }, 500);
    TEST_ASSERT_FALSE(c.status.present);
    TEST_ASSERT_EQUAL_INT(1, toc_attempts);

    media = (Ps2CdMedia){ .audio = 1 };

    tick(&c, (CdRequests){ 0 }, 12000);
    TEST_ASSERT_TRUE(c.status.present);
    TEST_ASSERT_FALSE(c.status.error[0]);
    TEST_ASSERT_EQUAL_UINT(generation + 1, c.status.generation);
    TEST_ASSERT_EQUAL_UINT(1, c.toc_attempts);

    toc_failures = 0;

    tick(&c, (CdRequests){ 0 }, 12500);
    TEST_ASSERT_EQUAL_INT(3, c.status.tracks);
}

/**
 * @brief Run playback-controller regressions with deterministic hardware substitutes.
 * @return Number of failed Unity cases.
 */
/** @brief Program snapshots record audible entries, leaving skips pending and resetting new programs. */
static void program_played_history(void)
{
    playing();
    tick(&c, (CdRequests){ .program = { 3, 1, 2 }, .program_count = 3 }, 2);
    TEST_ASSERT_TRUE(c.status.programmed[0]);
    TEST_ASSERT_TRUE(c.status.programmed[1]);
    TEST_ASSERT_TRUE(c.status.programmed[2]);
    TEST_ASSERT_FALSE(c.status.played[2]);

    c.submitted = c.toc.start[2] + 1;

    tick(&c, (CdRequests){ 0 }, 3);
    TEST_ASSERT_TRUE(c.status.played[2]);
    TEST_ASSERT_FALSE(c.status.played[0]);
    command(&c, AUDIO_TRANSPORT_NEXT, 4);
    TEST_ASSERT_EQUAL_INT(1, c.status.track);
    command(&c, AUDIO_TRANSPORT_NEXT, 5);
    TEST_ASSERT_EQUAL_INT(2, c.status.track);
    TEST_ASSERT_FALSE(c.status.played[0]);
    TEST_ASSERT_TRUE(c.status.played[2]);
    tick(&c, (CdRequests){ .program = { 1 }, .program_count = 1 }, 6);
    TEST_ASSERT_TRUE(c.status.programmed[0]);
    TEST_ASSERT_FALSE(c.status.programmed[1]);
    TEST_ASSERT_FALSE(c.status.programmed[2]);
    TEST_ASSERT_FALSE(c.status.played[2]);

    media = (Ps2CdMedia){ .changed = 1, .absent = 1 };

    tick(&c, (CdRequests){ 0 }, 500);
    TEST_ASSERT_FALSE(c.status.programmed[0]);
    TEST_ASSERT_FALSE(c.status.played[2]);
}

static void autoplay_disabled(void)
{
    controller_opened = 1;

    cd_controller_open(&c, &runtime, 0, 0);
    tick(&c, (CdRequests){ 0 }, 0);
    TEST_ASSERT_TRUE(c.status.present);
    TEST_ASSERT_EQUAL_INT(3, c.status.tracks);
    TEST_ASSERT_FALSE(c.status.playing);
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 1);
    TEST_ASSERT_TRUE(c.status.playing);

    media = (Ps2CdMedia){ .changed = 1, .absent = 1 };

    tick(&c, (CdRequests){ 0 }, 500);

    media = (Ps2CdMedia){ .changed = 1, .audio = 1 };

    tick(&c, (CdRequests){ 0 }, 1000);
    TEST_ASSERT_TRUE(c.status.present);
    TEST_ASSERT_FALSE(c.status.playing);
    command(&c, AUDIO_TRANSPORT_PLAY_PAUSE, 1001);
    TEST_ASSERT_TRUE(c.status.playing);
}

/** @brief Read failures caused by ejecting between media polls never become playback errors. */
static void eject_during_read_has_no_error(void)
{
    for (int prefetch = 0; prefetch <= 1; ++prefetch)
    {
        fixture();
        playing();

        media            = (Ps2CdMedia){ .changed = 1, .absent = 1 };
        read_failure     = !prefetch;
        prefetch_failure = prefetch;
        int stopped = output_stops, cleared = clears;

        tick(&c, (CdRequests){ 0 }, 2);
        TEST_ASSERT_FALSE(c.status.present);
        TEST_ASSERT_FALSE(c.status.playing);
        TEST_ASSERT_EQUAL_STRING("", c.status.error);
        TEST_ASSERT_EQUAL_INT(stopped + 1, output_stops);
        TEST_ASSERT_EQUAL_INT(cleared + 1, clears);

        prefetch_failure = 0;
        media            = (Ps2CdMedia){ .audio = 1 };

        tick(&c, (CdRequests){ 0 }, 502);
        TEST_ASSERT_TRUE(c.status.present);
        TEST_ASSERT_TRUE(c.status.playing);
        TEST_ASSERT_EQUAL_STRING("", c.status.error);
        close_controller();
    }
}

/** @brief An unchanged audio disc still reports a genuine read failure. */
static void read_failure_with_disc_present_is_error(void)
{
    for (int prefetch = 0; prefetch <= 1; ++prefetch)
    {
        fixture();
        playing();

        read_failure     = !prefetch;
        prefetch_failure = prefetch;

        tick(&c, (CdRequests){ 0 }, 2);
        TEST_ASSERT_TRUE(c.status.present);
        TEST_ASSERT_FALSE(c.status.playing);
        TEST_ASSERT_EQUAL_STRING("CD READ ERROR - PRESS PLAY TO RETRY", c.status.error);
        close_controller();
    }
}

/** @brief A read from the old disc cannot attach its failure to a newly detected disc. */
static void replacement_during_read_has_no_stale_error(void)
{
    playing();

    unsigned previous = c.status.generation;

    media        = (Ps2CdMedia){ .changed = 1, .audio = 1 };
    read_failure = 1;

    tick(&c, (CdRequests){ 0 }, 2);
    TEST_ASSERT_TRUE(c.status.present);
    TEST_ASSERT_TRUE(c.status.playing);
    TEST_ASSERT_NOT_EQUAL(previous, c.status.generation);
    TEST_ASSERT_EQUAL_STRING("", c.status.error);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(eject_during_read_has_no_error);
    RUN_TEST(read_failure_with_disc_present_is_error);
    RUN_TEST(replacement_during_read_has_no_stale_error);
    RUN_TEST(autoplay_disabled);
    RUN_TEST(stop_errors);
    RUN_TEST(boundary_stop_error);
    RUN_TEST(stop_scanning);
    RUN_TEST(underrun_position);
    RUN_TEST(playback_progress);
    RUN_TEST(replacement_playback);
    RUN_TEST(replacement_requests);
    RUN_TEST(seamless_modes);
    RUN_TEST(sound_retry);
    RUN_TEST(toc_retry);
    RUN_TEST(lid_close_toc_recovery);
    RUN_TEST(lid_reopen_during_toc);
    RUN_TEST(navigation);
    RUN_TEST(program_played_history);
    RUN_TEST(boundaries);
    RUN_TEST(completed_progress);
    RUN_TEST(failures_and_removal);
    RUN_TEST(output_queue_failures);
    RUN_TEST(restart_required_at_startup);
    RUN_TEST(unresponsive_drive);

    return UNITY_END();
}
