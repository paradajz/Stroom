#include "util/diagnostics.h"
#include "audio/cd/cd_output.h"
#include "platform/iop/modules.h"
#include "platform/time/clock.h"
#include <audsrv.h>
#include "unity.h"
#include "audio/output/device.h"
#include <kernel.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

unsigned char            audsrv_irx[1], freesd_irx[1];
unsigned int             size_audsrv_irx, size_freesd_irx;
static int               resample_calls;
static int               available;
static int               queued;
static int               written;
static int               stops;
static int               current_volume;
static int               stop_failure_at;
static int               volume_failure;
static int               init_failure, format_failure;
static uint32_t          now;
static volatile int      running = 1;
static audsrv_callback_t refill_callback;
static void*             refill_argument;
static int               callback_threshold, callback_failure;
static int               queue_queries, available_queries;
static int               notify_during_query;
static int               quit_failure, delete_failure, quits, semaphore_creations, semaphore_deletions, live_semaphores;

void __real_cd_resample(CdResampler* state, const uint8_t* sector, uint8_t* output);

void __wrap_cd_resample(CdResampler* state, const uint8_t* sector, uint8_t* output)
{
    ++resample_calls;
    __real_cd_resample(state, sector, output);
}

static void notify_refill(void)
{
    if (refill_callback && callback_threshold)
    {
        refill_callback(refill_argument);
    }
}

/* Model device progress plus the notification delivered to the EE. */
static void queue_depth(int bytes)
{
    queued = bytes;

    notify_refill();
}

int audsrv_on_fillbuf(int amount, audsrv_callback_t callback, void* argument)
{
    if (callback_failure)
    {
        return -1;
    }

    callback_threshold = amount;
    refill_callback    = callback;
    refill_argument    = argument;

    return 0;
}

int CreateSema(ee_sema_t* sema)
{
    (void)sema;
    TEST_ASSERT_EQUAL_INT(0, live_semaphores);
    ++live_semaphores;
    ++semaphore_creations;

    return 1;
}

int WaitSema(int id)
{
    (void)id;

    return 0;
}

int SignalSema(int id)
{
    (void)id;

    return 0;
}

int DeleteSema(int id)
{
    TEST_ASSERT_EQUAL_INT(1, id);
    ++semaphore_deletions;

    if (delete_failure)
    {
        return -1;
    }

    TEST_ASSERT_EQUAL_INT(1, live_semaphores);
    --live_semaphores;

    return 0;
}

/* Hardware and clock substitutes for the real cd_output.c implementation. */
int audsrv_init(void)
{
    return init_failure;
}

int audsrv_quit(void)
{
    ++quits;

    return quit_failure;
}

int audsrv_set_format(audsrv_fmt_t* format)
{
    TEST_ASSERT_TRUE_MESSAGE(format->freq == 48000 && format->bits == 16 && format->channels == 2, "format->freq == 48000 && format->bits == 16 && format->channels == 2");

    return format_failure;
}

int audsrv_set_volume(int volume)
{
    if (volume == volume_failure)
    {
        return -1;
    }

    current_volume = volume;

    return 0;
}

int audsrv_stop_audio(void)
{
    ++stops;

    if (stops == stop_failure_at)
    {
        return -1;
    }

    callback_threshold = 0;

    return 0;
}

int audsrv_available(void)
{
    ++available_queries;

    return available;
}

int audsrv_queued(void)
{
    ++queue_queries;

    if (notify_during_query)
    {
        notify_during_query = 0;

        notify_refill();
    }

    return queued;
}

int audsrv_play_audio(const char* data, int size)
{
    TEST_ASSERT_TRUE_MESSAGE(data && size > 0, "data && size > 0");

    written += size;

    return size;
}

int platform_iop_probe(unsigned id)
{
    (void)id;

    return 1;
}

int platform_iop_module(const Ps2IopModule* module, char* error, size_t capacity)
{
    (void)module;
    (void)error;
    (void)capacity;

    return 0;
}

uint32_t platform_millis(void)
{
    return now;
}

/**
 * @brief Advance simulated time.
 * @param delay Microseconds.
 * @return Zero on success.
 */
int usleep(useconds_t delay)
{
    now += delay / 1000;

    return 0;
}

/**
 * @brief Reset queue fixtures and observed output effects.
 */
static void fixture(void)
{
    init_failure = format_failure = 0;
    quit_failure = delete_failure = 0;
    callback_failure = notify_during_query = 0;
    stop_failure_at                        = 0;
    volume_failure                         = -1;
    available                              = 8192;
    queued = written = stops = current_volume = 0;
    now                                       = 0;

    TEST_ASSERT_TRUE(output_close() == 0);
    TEST_ASSERT_TRUE(output_open() == 0);
    output_select(OUTPUT_CD);

    char          error[64] = { 0 };
    OutputRuntime runtime   = { .running = &running, .error = error, .capacity = sizeof(error) };

    TEST_ASSERT_TRUE(output_initialize(&runtime) == 0);
    TEST_ASSERT_TRUE(output_prepare(OUTPUT_CD, &runtime) == 0);

    queue_queries = available_queries = 0;
    written = stops = current_volume = 0;
    now                              = 0;
}

/**
 * @brief Verify stop failures reach callers and unopened output needs no SDK call.
 */
static void stop_errors(void)
{
    CdOutput output = { 0 };

    TEST_ASSERT_TRUE(cd_output_stop(&output) == 0);
    TEST_ASSERT_EQUAL_INT(0, stops);

    output.ready    = 1;
    stop_failure_at = 1;

    TEST_ASSERT_TRUE(!(cd_output_stop(&output) == 0));
    TEST_ASSERT_EQUAL_INT(1, stops);
    TEST_ASSERT_TRUE(cd_output_stop(&output) == 0);
    TEST_ASSERT_EQUAL_INT(2, stops);
}

/**
 * @brief Verify invalid queue counts never become busy or successful writes.
 */
static void silence_errors(void)
{
    CdOutput output = { .ready = 1 };

    fixture();

    available = -1;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_silence(&output, 0) == CD_OUTPUT_ERROR_QUERY && written == 0, "cd_output_silence(&output, 0) == CD_OUTPUT_ERROR_QUERY && written == 0");

    available = 8192;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_silence(&output, CD_OUTPUT_ERROR_QUERY) == CD_OUTPUT_ERROR_QUERY && written == 0, "cd_output_silence(&output, CD_OUTPUT_ERROR_QUERY) == CD_OUTPUT_ERROR_QUERY && written == 0");

    available = 0;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_silence(&output, 0) == 1 && written == 0, "cd_output_silence(&output, 0) == 0 && written == 0");

    available = 8192;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_silence(&output, 8192) == 1 && written == 0, "cd_output_silence(&output, 8192) == 0 && written == 0");
    TEST_ASSERT_TRUE_MESSAGE(cd_output_silence(&output, 0) == 0 && written == 2048, "cd_output_silence(&output, 0) == 1 && written == 2048");
}

/**
 * @brief Verify flush failure is prompt and cannot be mistaken for a drained queue.
 */
static void flush_errors(void)
{
    CdOutput        output  = { .ready = 1 };
    const CdRuntime runtime = { .running = &running };

    fixture();

    available = -1;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_reset(&output, &runtime, 1) != 0, "!cd_output_reset(&output, &runtime, 1)");
    TEST_ASSERT_TRUE_MESSAGE(written == 0 && now == 0 && stops == 2, "written == 0 && now == 0 && stops == 2");
    fixture();
    queue_depth(-1);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_reset(&output, &runtime, 1) != 0, "!cd_output_reset(&output, &runtime, 1)");
    TEST_ASSERT_TRUE_MESSAGE(written == 20480 && current_volume == 0 && now < 1000 && stops == 2, "written == 20480 && current_volume == 0 && now < 1000 && stops == 2");
    fixture();
    TEST_ASSERT_TRUE_MESSAGE(cd_output_reset(&output, &runtime, 1) == 0, "cd_output_reset(&output, &runtime, 1)");
    TEST_ASSERT_TRUE_MESSAGE(written == 20480 && current_volume == MAX_VOLUME && now < 1000, "written == 20480 && current_volume == MAX_VOLUME && now < 1000");
    fixture();

    available = 0;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_reset(&output, &runtime, 1) != 0, "!cd_output_reset(&output, &runtime, 1)");
    TEST_ASSERT_TRUE_MESSAGE(written == 0 && now == 1000, "written == 0 && now == 1000"); /* Genuine backpressure still waits. */
}

/**
 * @brief Verify stop, mute, and unmute failures cannot report prepared output.
 */
static void preparation_control_errors(void)
{
    CdOutput        output  = { .ready = 1 };
    const CdRuntime runtime = { .running = &running };

    fixture();

    stop_failure_at = 1;
    current_volume  = MAX_VOLUME;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_reset(&output, &runtime, 1) != 0, "!cd_output_reset(&output, &runtime, 1)");
    TEST_ASSERT_TRUE_MESSAGE(written == 0 && stops == 1 && current_volume == MAX_VOLUME, "written == 0 && stops == 1 && current_volume == MAX_VOLUME");
    fixture();

    volume_failure = 0;
    current_volume = MAX_VOLUME;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_reset(&output, &runtime, 1) != 0, "!cd_output_reset(&output, &runtime, 1)");
    TEST_ASSERT_TRUE_MESSAGE(written == 0 && current_volume == MAX_VOLUME, "written == 0 && current_volume == MAX_VOLUME");
    fixture();

    stop_failure_at = 2;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_reset(&output, &runtime, 1) != 0, "!cd_output_reset(&output, &runtime, 1)");
    TEST_ASSERT_TRUE_MESSAGE(written == 20480 && stops == 3 && current_volume == 0, "written == 20480 && stops == 3 && current_volume == 0");
    fixture();

    volume_failure = MAX_VOLUME;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_reset(&output, &runtime, 1) != 0, "!cd_output_reset(&output, &runtime, 1)");
    TEST_ASSERT_TRUE_MESSAGE(written == 20480 && current_volume == 0 && stops == 3, "written == 20480 && current_volume == 0 && stops == 3");

    volume_failure = -1;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_reset(&output, &runtime, 1) == 0, "cd_output_reset(&output, &runtime, 1)");
    TEST_ASSERT_TRUE_MESSAGE(current_volume == MAX_VOLUME && written == 40960, "current_volume == MAX_VOLUME && written == 40960");
}

/**
 * @brief Check sector output retains its existing negative-result handling.
 */
static void sector_errors(void)
{
    CdOutput             output = { .ready = 1 };
    static const uint8_t sector[CD_SECTOR_BYTES];
    const uint8_t*       samples = NULL;

    fixture();
    queue_depth(-1);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_submit(&output, sector, &samples) == CD_OUTPUT_ERROR_QUERY && written == 0, "cd_output_submit(&output, sector, &samples) == CD_OUTPUT_ERROR_QUERY && written == 0");
    queue_depth(0);

    available = -1;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_submit(&output, sector, &samples) == CD_OUTPUT_ERROR_QUERY && written == 0, "cd_output_submit(&output, sector, &samples) == CD_OUTPUT_ERROR_QUERY && written == 0");

    available = 8192;

    TEST_ASSERT_TRUE_MESSAGE(cd_output_submit(&output, sector, &samples) == 0 && written == CD_OUTPUT_BYTES && samples, "cd_output_submit(&output, sector, &samples) == 1 && written == CD_OUTPUT_BYTES && samples");
}

/**
 * @brief Busy retries reuse PCM and commit interpolation history only on acceptance.
 */
static void pending_sector(void)
{
    CdOutput        output  = { .ready = 1 };
    const CdRuntime runtime = { .running = &running };
    uint8_t         sector[CD_SECTOR_BYTES];
    uint8_t         expected[CD_OUTPUT_BYTES];
    CdResampler     reference = { 0 };
    const uint8_t*  samples   = NULL;

    fixture();
    memset(sector, 0x12, sizeof(sector));
    __real_cd_resample(&reference, sector, expected);

    resample_calls = 0;

    queue_depth(OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES + 1);

    for (int i = 0; i < 20; ++i)
    {
        TEST_ASSERT_EQUAL_INT(1, cd_output_submit(&output, sector, &samples));
    }

    TEST_ASSERT_EQUAL_INT(1, resample_calls);
    TEST_ASSERT_EQUAL_INT(0, written);
    TEST_ASSERT_NULL(samples);
    TEST_ASSERT_EQUAL_INT(0, output.resampler.primed);
    queue_depth(0);
    TEST_ASSERT_EQUAL_INT(0, cd_output_submit(&output, sector, &samples));
    TEST_ASSERT_EQUAL_INT(1, resample_calls);
    TEST_ASSERT_EQUAL_MEMORY(expected, samples, sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY(&reference, &output.resampler, sizeof(reference));
    TEST_ASSERT_EQUAL_INT(1, output.block_count);
    memset(sector, 0x34, sizeof(sector));
    __real_cd_resample(&reference, sector, expected);
    queue_depth(0);
    TEST_ASSERT_EQUAL_INT(0, cd_output_submit(&output, sector, &samples));
    TEST_ASSERT_EQUAL_INT(2, resample_calls);
    TEST_ASSERT_EQUAL_MEMORY(expected, samples, sizeof(expected));
    queue_depth(OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES + 1);
    TEST_ASSERT_EQUAL_INT(1, cd_output_submit(&output, sector, &samples));
    TEST_ASSERT_EQUAL_INT(3, resample_calls);
    TEST_ASSERT_TRUE(cd_output_reset(&output, &runtime, 0) == 0);
    memset(sector, 0x56, sizeof(sector));
    memset(&reference, 0, sizeof(reference));
    __real_cd_resample(&reference, sector, expected);
    queue_depth(0);
    TEST_ASSERT_EQUAL_INT(0, cd_output_submit(&output, sector, &samples));
    TEST_ASSERT_EQUAL_INT(4, resample_calls);
    TEST_ASSERT_EQUAL_MEMORY(expected, samples, sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY(&reference, &output.resampler, sizeof(reference));
    queue_depth(OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES + 1);
    TEST_ASSERT_EQUAL_INT(1, cd_output_submit(&output, sector, &samples));
    cd_output_close(&output);
    TEST_ASSERT_FALSE(output.pending);
}

/**
 * @brief Count only CD bytes in partially drained, mixed audio/silence queues.
 */
static void queue_accounting(void)
{
    CdOutput             output  = { .ready = 1 };
    const CdRuntime      runtime = { .running = &running };
    static const uint8_t sector[CD_SECTOR_BYTES];
    const uint8_t*       samples;
    int                  audio;

    fixture();
    queue_depth(2048); /* Silent lead-in predates the first CD submission. */
    TEST_ASSERT_TRUE_MESSAGE(cd_output_queued(&output, &audio) == 2048 && audio == 0, "cd_output_queued(&output, &audio) == 2048 && audio == 0");
    queue_depth(0);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_submit(&output, sector, &samples) == 0, "cd_output_submit(&output, sector, &samples) == 1");
    TEST_ASSERT_TRUE_MESSAGE(cd_output_silence(&output, 0) == 0, "cd_output_silence(&output, 0) == 1");
    queue_depth(CD_OUTPUT_BYTES + 2048);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_queued(&output, &audio) == queued && audio == CD_OUTPUT_BYTES, "cd_output_queued(&output, &audio) == queued && audio == CD_OUTPUT_BYTES");
    queue_depth(2048 + 1280);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_queued(&output, &audio) == queued && audio == 1280, "cd_output_queued(&output, &audio) == queued && audio == 1280");
    queue_depth(2048);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_queued(&output, &audio) == queued && audio == 0, "cd_output_queued(&output, &audio) == queued && audio == 0");
    queue_depth(0);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_submit(&output, sector, &samples) == 0, "cd_output_submit(&output, sector, &samples) == 1");
    queue_depth(CD_OUTPUT_BYTES + 1024);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_queued(&output, &audio) == queued && audio == CD_OUTPUT_BYTES, "cd_output_queued(&output, &audio) == queued && audio == CD_OUTPUT_BYTES");
    queue_depth(1280);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_queued(&output, &audio) == queued && audio == 1280, "cd_output_queued(&output, &audio) == queued && audio == 1280");
    queue_depth(0);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_queued(&output, &audio) == 0 && audio == 0, "cd_output_queued(&output, &audio) == 0 && audio == 0");

    for (int i = 0; i < CD_OUTPUT_BLOCKS + 2; ++i)
    {
        queue_depth(0);
        TEST_ASSERT_TRUE_MESSAGE(cd_output_silence(&output, 0) == 0, "cd_output_silence(&output, 0) == 1");
    }

    TEST_ASSERT_TRUE_MESSAGE(cd_output_submit(&output, sector, &samples) == 0, "cd_output_submit(&output, sector, &samples) == 1");
    queue_depth(6144);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_queued(&output, &audio) == queued && audio == CD_OUTPUT_BYTES, "cd_output_queued(&output, &audio) == queued && audio == CD_OUTPUT_BYTES");
    queue_depth(-1);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_queued(&output, &audio) == CD_OUTPUT_ERROR_QUERY && audio == 0, "cd_output_queued(&output, &audio) == CD_OUTPUT_ERROR_QUERY && audio == 0");
    TEST_ASSERT_TRUE_MESSAGE(cd_output_reset(&output, &runtime, 0) == 0, "cd_output_reset(&output, &runtime, 0)");
    queue_depth(2048);
    TEST_ASSERT_TRUE_MESSAGE(cd_output_queued(&output, &audio) == queued && audio == 0, "cd_output_queued(&output, &audio) == queued && audio == 0");
}

/**
 * @brief An inactive CD cannot initialize over, write to, or stop network sound.
 */
static void shared_ownership(void)
{
    char          error[64] = { 0 };
    OutputRuntime runtime   = { .running = &running, .error = error, .capacity = sizeof(error) };

    output_select(OUTPUT_NETWORK);
    TEST_ASSERT_TRUE(output_prepare(OUTPUT_NETWORK, &runtime) == 0);

    written = stops = 0;

    CdOutput         cd        = { 0 };
    CdPlaybackStatus status    = { 0 };
    CdRuntime        callbacks = { .running = &running };

    TEST_ASSERT_TRUE(cd_output_open(&cd, &callbacks, &status) == 0);
    TEST_ASSERT_EQUAL_INT(0, stops);
    TEST_ASSERT_TRUE(cd_output_stop(&cd) == 0);
    cd_output_close(&cd);
    TEST_ASSERT_EQUAL_INT(0, stops);

    uint8_t pcm[32] = { 0 };

    TEST_ASSERT_EQUAL_INT(1, output_write(OUTPUT_CD, pcm, sizeof(pcm)));
    queue_depth(128);
    TEST_ASSERT_EQUAL_INT(0, output_write(OUTPUT_NETWORK, pcm, sizeof(pcm)));
    queue_depth(-1);
    TEST_ASSERT_EQUAL_INT(OUTPUT_ERROR_QUERY, output_write(OUTPUT_NETWORK, pcm, sizeof(pcm)));
    queue_depth(0);
    TEST_ASSERT_EQUAL_INT(sizeof(pcm), written);
    output_select(OUTPUT_CD);
    TEST_ASSERT_EQUAL_INT(1, output_write(OUTPUT_NETWORK, pcm, sizeof(pcm)));
    TEST_ASSERT_TRUE(output_prepare(OUTPUT_CD, &runtime) == 0);

    stops = 0;

    TEST_ASSERT_TRUE(output_stop(OUTPUT_NETWORK) == 0);
    TEST_ASSERT_EQUAL_INT(0, stops);
    TEST_ASSERT_EQUAL_INT(0, output_write(OUTPUT_CD, pcm, sizeof(pcm)));
}

#if STROOM_DIAGNOSTICS
/** The production diagnostic API reports admission without changing write behavior. */
static void timed_admission(void)
{
    OutputRuntime runtime = { .running = &running };

    output_select(OUTPUT_NETWORK);
    TEST_ASSERT_TRUE(output_prepare(OUTPUT_NETWORK, &runtime) == 0);

    written = 0;

    uint8_t           pcm[32] = { 0 };
    OutputWriteTiming timing;

    queue_depth(128);
    TEST_ASSERT_EQUAL_INT(0, output_write_timed(OUTPUT_NETWORK, pcm, sizeof(pcm), &timing));
    TEST_ASSERT_EQUAL_INT(128, timing.queued);
    TEST_ASSERT_EQUAL_INT(sizeof(pcm), written);
    queue_depth(OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES + 1);
    TEST_ASSERT_EQUAL_INT(1, output_write_timed(OUTPUT_NETWORK, pcm, sizeof(pcm), &timing));
    TEST_ASSERT_EQUAL_INT(queued, timing.queued);
    queue_depth(-1);
    TEST_ASSERT_EQUAL_INT(OUTPUT_ERROR_QUERY, output_write_timed(OUTPUT_NETWORK, pcm, sizeof(pcm), &timing));
    TEST_ASSERT_EQUAL_INT(OUTPUT_ERROR_QUERY, timing.queued);
    output_select(OUTPUT_CD);
    queue_depth(128);
    TEST_ASSERT_EQUAL_INT(1, output_write_timed(OUTPUT_NETWORK, pcm, sizeof(pcm), &timing));
    TEST_ASSERT_EQUAL_INT(-1, timing.queued);
    TEST_ASSERT_EQUAL_INT(sizeof(pcm), written);
}
#endif

/**
 * @brief Fixtures are initialized by each scenario.
 */
void setUp(void)
{
    running = 1;

    fixture();
}

/**
 * @brief This suite owns no external resources.
 */
void tearDown(void)
{
    quit_failure = delete_failure = stop_failure_at = 0;

    TEST_ASSERT_TRUE(output_close() == 0);
    TEST_ASSERT_EQUAL_INT(0, live_semaphores);
}

/**
 * @brief Run the regression scenario.
 * @return Number of failed Unity cases.
 */
static void mute_preserves_writes_and_survives_preparation(void)
{
    OutputRuntime runtime = { .running = &running };
    uint8_t       pcm[32] = { 0 };

    current_volume = MAX_VOLUME;

    for (int owner = OUTPUT_CD; owner <= OUTPUT_NETWORK; ++owner)
    {
        output_select(owner);
        TEST_ASSERT_TRUE(output_prepare(owner, &runtime) == 0);
        TEST_ASSERT_EQUAL_INT(MAX_VOLUME, current_volume);
        output_set_muted(1);
        TEST_ASSERT_EQUAL_INT(MAX_VOLUME, current_volume); /* No device RPC on the UI thread. */
        queue_depth(128);

        int before = written;

        TEST_ASSERT_EQUAL_INT(0, output_write(owner, pcm, sizeof(pcm)));
        TEST_ASSERT_EQUAL_INT(before + sizeof(pcm), written);
        TEST_ASSERT_EQUAL_INT(0, current_volume);
        TEST_ASSERT_TRUE(output_prepare(owner, &runtime) == 0);
        TEST_ASSERT_EQUAL_INT(0, current_volume);
        output_set_muted(0);
        queue_depth(0);
        TEST_ASSERT_EQUAL_INT(0, output_queued(owner));
        TEST_ASSERT_EQUAL_INT(MAX_VOLUME, current_volume);
    }
}

/** Busy workers and CD position reads share a snapshot until the IOP notifies. */
static void callback_admission(void)
{
    uint8_t pcm[2560] = { 0 };

    TEST_ASSERT_EQUAL_INT(16384, callback_threshold);
    queue_depth(6144);
    TEST_ASSERT_EQUAL_INT(1, output_write(OUTPUT_CD, pcm, sizeof(pcm)));

    int queries          = queue_queries;
    int capacity_queries = available_queries;

    queued = 4096; /* Hardware changed, but no callback has arrived. */

    for (int i = 0; i < 20; ++i)
    {
        TEST_ASSERT_EQUAL_INT(1, output_write(OUTPUT_CD, pcm, sizeof(pcm)));
        TEST_ASSERT_EQUAL_INT(6144, output_queued(OUTPUT_CD));
    }

    TEST_ASSERT_EQUAL_INT(queries, queue_queries);
    TEST_ASSERT_EQUAL_INT(capacity_queries, available_queries);
    notify_refill();
    notify_refill(); /* Repeated callbacks coalesce. */
    TEST_ASSERT_EQUAL_INT(0, output_write(OUTPUT_CD, pcm, sizeof(pcm)));
    TEST_ASSERT_EQUAL_INT(queries + 1, queue_queries);
    TEST_ASSERT_EQUAL_INT(capacity_queries + 1, available_queries);
    TEST_ASSERT_EQUAL_INT(4096 + sizeof(pcm), output_queued(OUTPUT_CD));
    TEST_ASSERT_EQUAL_INT(1, output_write(OUTPUT_CD, pcm, sizeof(pcm)));
    TEST_ASSERT_EQUAL_INT(queries + 1, queue_queries);
    output_set_muted(1);
    TEST_ASSERT_EQUAL_INT(1, output_write(OUTPUT_CD, pcm, sizeof(pcm)));
    TEST_ASSERT_EQUAL_INT(0, current_volume);
}

/** Notifications arriving during a sound RPC cannot be lost. */
static void callback_during_query(void)
{
    uint8_t pcm[2560] = { 0 };

    queue_depth(6144);

    notify_during_query = 1;

    TEST_ASSERT_EQUAL_INT(1, output_write(OUTPUT_CD, pcm, sizeof(pcm)));

    int queries = queue_queries;

    queued = 4096;

    TEST_ASSERT_EQUAL_INT(0, output_write(OUTPUT_CD, pcm, sizeof(pcm)));
    TEST_ASSERT_EQUAL_INT(queries + 1, queue_queries);
}

/** Stops clear notifications; preparation rearms and primes the new owner. */
static void callback_lifecycle(void)
{
    uint8_t       pcm[2560] = { 0 };
    OutputRuntime runtime   = { .running = &running };

    TEST_ASSERT_TRUE(output_stop(OUTPUT_CD) == 0);
    TEST_ASSERT_EQUAL_INT(0, callback_threshold);
    refill_callback(refill_argument); /* Late notification from the old session. */
    TEST_ASSERT_EQUAL_INT(1, output_write(OUTPUT_CD, pcm, sizeof(pcm)));
    output_select(OUTPUT_NETWORK);
    TEST_ASSERT_TRUE(output_prepare(OUTPUT_NETWORK, &runtime) == 0);
    TEST_ASSERT_EQUAL_INT(16384, callback_threshold);

    int queries = queue_queries;

    TEST_ASSERT_EQUAL_INT(0, output_write(OUTPUT_NETWORK, pcm, sizeof(pcm)));
    TEST_ASSERT_EQUAL_INT(queries + 1, queue_queries); /* No callback needed to start. */
    TEST_ASSERT_TRUE(output_stop(OUTPUT_CD) == 0);
    TEST_ASSERT_EQUAL_INT(16384, callback_threshold); /* Inactive owner cannot disarm. */

    callback_failure = 1;

    TEST_ASSERT_TRUE(!(output_prepare(OUTPUT_NETWORK, &runtime) == 0));
    TEST_ASSERT_EQUAL_INT(0, callback_threshold);
    TEST_ASSERT_EQUAL_INT(1, output_write(OUTPUT_NETWORK, pcm, sizeof(pcm)));

    callback_failure = 0;

    TEST_ASSERT_TRUE(output_prepare(OUTPUT_NETWORK, &runtime) == 0);
    TEST_ASSERT_EQUAL_INT(0, output_write(OUTPUT_NETWORK, pcm, sizeof(pcm)));
}

/** @brief Failed stop preserves the service and semaphore and prevents reopening. */
static void close_stop_failure_retries(void)
{
    int prior_quits     = quits;
    int prior_deletions = semaphore_deletions;
    int prior_creations = semaphore_creations;
    stop_failure_at     = stops + 1;

    TEST_ASSERT_TRUE(!(output_close() == 0));
    TEST_ASSERT_EQUAL_INT(prior_quits, quits);
    TEST_ASSERT_EQUAL_INT(prior_deletions, semaphore_deletions);
    TEST_ASSERT_EQUAL_INT(1, live_semaphores);
    TEST_ASSERT_TRUE(!(output_open() == 0));
    TEST_ASSERT_EQUAL_INT(prior_creations, semaphore_creations);
    TEST_ASSERT_TRUE(output_close() == 0);
    TEST_ASSERT_EQUAL_INT(prior_quits + 1, quits);
    TEST_ASSERT_EQUAL_INT(prior_deletions + 1, semaphore_deletions);
    TEST_ASSERT_EQUAL_INT(0, live_semaphores);
    TEST_ASSERT_TRUE(output_close() == 0);
    TEST_ASSERT_EQUAL_INT(prior_quits + 1, quits);
    TEST_ASSERT_TRUE(output_open() == 0);
    TEST_ASSERT_EQUAL_INT(prior_creations + 1, semaphore_creations);
}

/** @brief Failed service shutdown retries quit without repeating a completed stop. */
static void close_quit_failure_retries(void)
{
    int prior_deletions = semaphore_deletions;
    int prior_stops     = stops;
    int prior_quits     = quits;
    quit_failure        = -9;

    TEST_ASSERT_TRUE(!(output_close() == 0));
    TEST_ASSERT_EQUAL_INT(prior_stops + 1, stops);
    TEST_ASSERT_EQUAL_INT(prior_quits + 1, quits);
    TEST_ASSERT_EQUAL_INT(prior_deletions, semaphore_deletions);
    TEST_ASSERT_EQUAL_INT(1, live_semaphores);
    TEST_ASSERT_TRUE(!(output_open() == 0));

    quit_failure = 0;

    TEST_ASSERT_TRUE(output_close() == 0);
    TEST_ASSERT_EQUAL_INT(prior_stops + 1, stops);
    TEST_ASSERT_EQUAL_INT(prior_quits + 2, quits);
    TEST_ASSERT_EQUAL_INT(0, live_semaphores);
}

/** @brief A retained semaphore is retried without calling a service that already quit. */
static void close_semaphore_failure_retries(void)
{
    int prior_quits     = quits;
    int prior_stops     = stops;
    int prior_creations = semaphore_creations;
    delete_failure      = 1;

    TEST_ASSERT_TRUE(!(output_close() == 0));
    TEST_ASSERT_EQUAL_INT(prior_quits + 1, quits);
    TEST_ASSERT_EQUAL_INT(prior_stops + 1, stops);
    TEST_ASSERT_EQUAL_INT(1, live_semaphores);
    TEST_ASSERT_TRUE(!(output_open() == 0));
    TEST_ASSERT_EQUAL_INT(prior_creations, semaphore_creations);
    TEST_ASSERT_TRUE(!(output_close() == 0));
    TEST_ASSERT_EQUAL_INT(prior_quits + 1, quits);
    TEST_ASSERT_EQUAL_INT(prior_stops + 1, stops);

    delete_failure = 0;

    TEST_ASSERT_TRUE(output_close() == 0);
    TEST_ASSERT_EQUAL_INT(0, live_semaphores);
    TEST_ASSERT_TRUE(output_open() == 0);
    TEST_ASSERT_EQUAL_INT(prior_creations + 1, semaphore_creations);
}

/** @brief Failed startup rollback retains ownership and blocks further RPC use. */
static void failed_startup_rollback_is_retried_by_close(void)
{
    const int failures[] = { OUTPUT_ERROR_INITIALIZE, OUTPUT_ERROR_FORMAT, OUTPUT_ERROR_VOLUME };

    for (int failure = 0; failure < 3; ++failure)
    {
        TEST_ASSERT_TRUE(output_close() == 0);
        TEST_ASSERT_TRUE(output_open() == 0);

        init_failure              = failure == 0 ? -2 : 0;
        format_failure            = failure == 1 ? -3 : 0;
        volume_failure            = failure == 2 ? MAX_VOLUME : -1;
        quit_failure              = -9;
        int           prior_quits = quits;
        int           prior_stops = stops;
        char          error[64]   = { 0 };
        OutputRuntime runtime     = { .running = &running, .error = error, .capacity = sizeof(error) };

        TEST_ASSERT_EQUAL_INT(failures[failure], output_initialize(&runtime));
        TEST_ASSERT_EQUAL_INT(prior_quits + 1, quits);
        TEST_ASSERT_TRUE(strstr(error, failure == 0 ? "SOUND INIT ERROR" : failure == 1 ? "SOUND FORMAT ERROR"
                                                                                        : "SOUND VOLUME ERROR") != NULL);
        TEST_ASSERT_EQUAL_INT(OUTPUT_ERROR_CLEANUP_PENDING, output_open());
        TEST_ASSERT_EQUAL_INT(OUTPUT_ERROR_CLEANUP_PENDING, output_initialize(&runtime));
        TEST_ASSERT_EQUAL_INT(prior_quits + 1, quits);
        TEST_ASSERT_EQUAL_INT(OUTPUT_ERROR_QUIT, output_close());
        TEST_ASSERT_EQUAL_INT(prior_quits + 2, quits);
        TEST_ASSERT_EQUAL_INT(prior_stops, stops);
        TEST_ASSERT_EQUAL_INT(1, live_semaphores);

        quit_failure = init_failure = format_failure = 0;
        volume_failure                               = -1;

        TEST_ASSERT_TRUE(output_close() == 0);
        TEST_ASSERT_EQUAL_INT(prior_quits + 3, quits);
        TEST_ASSERT_EQUAL_INT(0, live_semaphores);
        TEST_ASSERT_TRUE(output_open() == 0);
        TEST_ASSERT_TRUE(output_initialize(&runtime) == 0);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(close_stop_failure_retries);
    RUN_TEST(close_quit_failure_retries);
    RUN_TEST(close_semaphore_failure_retries);
    RUN_TEST(failed_startup_rollback_is_retried_by_close);
    RUN_TEST(callback_admission);
    RUN_TEST(callback_during_query);
    RUN_TEST(callback_lifecycle);
    RUN_TEST(mute_preserves_writes_and_survives_preparation);
    RUN_TEST(stop_errors);
    RUN_TEST(shared_ownership);
#if STROOM_DIAGNOSTICS
    RUN_TEST(timed_admission);
#endif
    RUN_TEST(queue_accounting);
    RUN_TEST(silence_errors);
    RUN_TEST(flush_errors);
    RUN_TEST(preparation_control_errors);
    RUN_TEST(sector_errors);
    RUN_TEST(pending_sector);

    return UNITY_END();
}
