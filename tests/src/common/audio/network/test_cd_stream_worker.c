#include "audio/network/cd_stream/stream.h"
#include "audio/network/cd_stream/server.h"
#include "audio/cd/cd_pcm.h"
#include "audio/cd/cd_runtime.h"
#include "platform/thread/worker.h"
#include "platform/network/runtime.h"
#include "unity.h"
#include <string.h>

static Ps2Worker* owner;
static void (*entry)(void*);
static uint32_t       now;
static int            network_result, server_result, worker_result, cleanup_result, network_cleanup_result;
static unsigned       starts, releases, socket_opens, socket_closes, pushes, resets, endings, steps;
static unsigned       sleep_count, waits, notifications;
static CdStreamSource source_observed[16];
static int            transport_failure;
static int            verify_complete_blocks;
static void (*during_sleep)(void);
static void (*during_network_startup)(void);
static uint8_t observed_blocks[CD_STREAM_BLOCKS + 1];
static uint8_t pcm[CD_OUTPUT_BYTES], observed[CD_OUTPUT_BYTES];

/**
 * @brief Substitute platform_network_startup in this host fixture.
 */
int platform_network_startup(char* error, size_t capacity)
{
    (void)capacity;

    error[0] = 0;

    ++starts;

    if (during_network_startup)
    {
        during_network_startup();
    }

    return network_result;
}

/**
 * @brief Substitute platform_network_close in this host fixture.
 */
int platform_network_close(void)
{
    ++releases;

    return network_cleanup_result;
}

/**
 * @brief Substitute platform_network_address in this host fixture.
 */
void platform_network_address(char* address, size_t capacity)
{
    (void)capacity;
    strcpy(address, "192.168.1.7");
}

/**
 * @brief Substitute platform_socket_nonblocking in this host fixture.
 */
int platform_socket_nonblocking(int fd)
{
    (void)fd;

    return 0;
}

/**
 * @brief Substitute platform_millis in this host fixture.
 */
uint32_t platform_millis(void)
{
    return now;
}

/**
 * @brief Substitute platform_worker_open in this host fixture.
 */
int platform_worker_open(Ps2Worker* worker, const Ps2WorkerConfig* config, Ps2WorkerError* error)
{
    (void)error;

    owner           = worker;
    entry           = config->entry;
    worker->lock    = 1;
    worker->thread  = 1;
    worker->running = worker_result == 0;

    return worker_result;
}

/**
 * @brief Substitute platform_worker_close in this host fixture.
 */
int platform_worker_close(Ps2Worker* worker)
{
    worker->running = 0;

    if (cleanup_result)
    {
        return cleanup_result;
    }

    *worker = (Ps2Worker)PS2_WORKER_INITIALIZER;

    return 0;
}

/**
 * @brief Substitute platform_worker_lock in this host fixture.
 */
void platform_worker_lock(const Ps2Worker* worker)
{
    TEST_ASSERT_EQUAL_INT(1, worker->lock);
}

/**
 * @brief Substitute platform_worker_unlock in this host fixture.
 */
void platform_worker_unlock(const Ps2Worker* worker)
{
    TEST_ASSERT_EQUAL_INT(1, worker->lock);
}

/**
 * @brief Substitute platform_worker_finish in this host fixture.
 */
void platform_worker_finish(Ps2Worker* worker)
{
    worker->running = 0;
}

/**
 * @brief Substitute platform_worker_wait in this host fixture.
 */
void platform_worker_wait(const Ps2Worker* worker)
{
    TEST_ASSERT_EQUAL_PTR(owner, worker);
    ++waits;

    owner->running = 0;
}

/**
 * @brief Substitute platform_worker_notify in this host fixture.
 */
void platform_worker_notify(const Ps2Worker* worker)
{
    TEST_ASSERT_EQUAL_PTR(owner, worker);
    ++notifications;
}

/**
 * @brief Substitute platform_sleep_us in this host fixture.
 */
void platform_sleep_us(uint32_t us)
{
    TEST_ASSERT_EQUAL_UINT(10000, us);
    ++sleep_count;

    if (during_sleep)
    {
        during_sleep();
    }

    if (sleep_count >= 3)
    {
        owner->running = 0;
    }
}

/**
 * @brief Substitute cd_stream_server_open in this host fixture.
 */
int cd_stream_server_open(CdStreamServer* server, int (*nonblocking)(int))
{
    (void)server;
    (void)nonblocking;
    ++socket_opens;

    return server_result;
}

/**
 * @brief Substitute cd_stream_server_overflows in this host fixture.
 */
void cd_stream_server_overflows(CdStreamServer* server, unsigned count)
{
    (void)server;
    TEST_ASSERT_GREATER_THAN_UINT(0, count);
}

/**
 * @brief Substitute cd_stream_server_close in this host fixture.
 */
void cd_stream_server_close(CdStreamServer* server)
{
    (void)server;
    ++socket_closes;
}

/**
 * @brief Substitute cd_stream_server_reset in this host fixture.
 */
void cd_stream_server_reset(CdStreamServer* server)
{
    (void)server;
    ++resets;
}

/**
 * @brief Substitute cd_stream_server_end in this host fixture.
 */
void cd_stream_server_end(CdStreamServer* server)
{
    (void)server;
    ++endings;
}

/**
 * @brief Substitute cd_stream_server_push in this host fixture.
 */
void cd_stream_server_push(CdStreamServer* server, const uint8_t* pcm)
{
    (void)server;
    TEST_ASSERT_LESS_THAN_UINT(sizeof(observed_blocks), pushes);

    observed_blocks[pushes] = pcm[0];

    ++pushes;

    if (verify_complete_blocks)
    {
        uint8_t expected[CD_OUTPUT_BYTES];

        memset(expected, pcm[0], sizeof(expected));
        TEST_ASSERT_EQUAL_MEMORY(expected, pcm, sizeof(expected));
    }

    memcpy(observed, pcm, sizeof(observed));
}

/**
 * @brief Substitute cd_stream_server_clients in this host fixture.
 */
unsigned cd_stream_server_clients(const CdStreamServer* server)
{
    (void)server;

    return 2;
}

/**
 * @brief Substitute cd_stream_server_step in this host fixture.
 */
int cd_stream_server_step(CdStreamServer* server, uint32_t now, const char* address)
{
    (void)server;
    (void)now;
    TEST_ASSERT_EQUAL_STRING("192.168.1.7", address);
    TEST_ASSERT_LESS_THAN_UINT(16, steps);

    source_observed[steps++] = server->source;

    return transport_failure ? -1 : 0;
}

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    network_cleanup_result = 0;
    cleanup_result         = 0;
    worker_result          = 0;
    server_result          = 0;
    network_result         = 0;
    transport_failure      = 0;
    verify_complete_blocks = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_stream_close());

    sleep_count            = 0;
    steps                  = 0;
    endings                = 0;
    resets                 = 0;
    pushes                 = 0;
    socket_closes          = 0;
    socket_opens           = 0;
    releases               = 0;
    starts                 = 0;
    during_sleep           = NULL;
    during_network_startup = NULL;
    waits                  = 0;
    notifications          = 0;
    now += 2000;

    memset(pcm, 0xa7, sizeof(pcm));
    memset(observed, 0, sizeof(observed));
}

/**
 * @brief Finish fixture cleanup and resource checks after each test.
 */
void tearDown(void)
{
    network_cleanup_result = 0;
    cleanup_result         = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_stream_close());
}

/**
 * @brief Verify failed startup cleans before retry.
 */
static void failed_startup_cleans_before_retry(void)
{
    server_result = -1;

    TEST_ASSERT_TRUE(cd_stream_update(1).failed);
    TEST_ASSERT_EQUAL_UINT(1, starts);

    cleanup_result = 1;

    TEST_ASSERT_TRUE(cd_stream_update(1).failed);
    TEST_ASSERT_EQUAL_UINT(0, releases);

    now += 2000;

    TEST_ASSERT_TRUE(cd_stream_update(1).failed);
    TEST_ASSERT_EQUAL_UINT(1, starts); /* Pending cleanup prevents reopening. */

    cleanup_result = 0;
    server_result  = 0;

    TEST_ASSERT_FALSE(cd_stream_update(1).failed);
    TEST_ASSERT_EQUAL_UINT(1, releases);
    TEST_ASSERT_EQUAL_UINT(2, starts);
}

/**
 * @brief Verify worker failure retains resources until cleanup.
 */
static void worker_failure_retains_resources_until_cleanup(void)
{
    worker_result = -1;

    TEST_ASSERT_TRUE(cd_stream_update(1).failed);

    cleanup_result = -1;

    TEST_ASSERT_EQUAL_INT(CD_STREAM_ERROR_CLOSE, cd_stream_close());
    TEST_ASSERT_EQUAL_UINT(0, releases);

    cleanup_result = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_stream_close());
    TEST_ASSERT_EQUAL_UINT(0, releases); /* No network reference was acquired. */

    worker_result = 0;
    server_result = -1;
    now += 2000;

    TEST_ASSERT_TRUE(cd_stream_update(1).failed);

    network_cleanup_result = -1;

    TEST_ASSERT_EQUAL_INT(CD_STREAM_ERROR_CLOSE, cd_stream_close());
    TEST_ASSERT_EQUAL_UINT(1, socket_closes);

    network_cleanup_result = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_stream_close());
    TEST_ASSERT_EQUAL_UINT(1, socket_closes); /* Completed cleanup is not repeated. */
}

/**
 * @brief Publish fixture PCM and mark natural playback completion.
 */
static void produce_and_finish(void)
{
    if (sleep_count == 1)
    {
        cd_stream_push(pcm, CD_OUTPUT_FRAMES);
        cd_stream_end();
        memset(pcm, 0x55, sizeof(pcm)); /* Copy lifetime must not borrow producer memory. */
    }
}

/**
 * @brief Verify callbacks copy pcm and finish on network worker.
 */
static void callbacks_copy_pcm_and_finish_on_network_worker(void)
{
    TEST_ASSERT_FALSE(cd_stream_update(1).failed);

    during_sleep = produce_and_finish;

    entry(NULL);
    TEST_ASSERT_EQUAL_UINT(1, pushes);
    TEST_ASSERT_EQUAL_UINT(2, endings);

    uint8_t expected[CD_OUTPUT_BYTES];

    memset(expected, 0xa7, sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY(expected, observed, sizeof(expected));
}

/**
 * @brief Publish autoplay PCM while networking is being initialized.
 */
static void autoplay_during_network_startup(void)
{
    cd_stream_reset();

    /* The CD worker can submit half a second of audio while cold-boot network
     * initialization blocks the application thread. */

    for (unsigned i = 0; i < 40; ++i)
    {
        memset(pcm, (int)i, sizeof(pcm));
        cd_stream_push(pcm, CD_OUTPUT_FRAMES);

        now += 14;
    }
}

/**
 * @brief Verify two seconds of opening audio survive a delayed sender.
 */
static void two_seconds_of_opening_audio_survive_a_delayed_sender(void)
{
    TEST_ASSERT_FALSE(cd_stream_update(1).failed);

    verify_complete_blocks = 1;

    for (unsigned i = 0; i < 150; ++i)
    {
        memset(pcm, (int)i, sizeof(pcm));
        cd_stream_push(pcm, CD_OUTPUT_FRAMES);
    }

    entry(NULL);
    TEST_ASSERT_EQUAL_UINT(150, pushes);

    for (unsigned i = 0; i < pushes; ++i)
    {
        TEST_ASSERT_EQUAL_UINT8(i, observed_blocks[i]);
    }
}

/**
 * @brief Verify autoplay opening survives slow network startup.
 */
static void autoplay_opening_survives_slow_network_startup(void)
{
    verify_complete_blocks = 1;
    during_network_startup = autoplay_during_network_startup;

    TEST_ASSERT_FALSE(cd_stream_update(1).failed);
    memset(pcm, 40, sizeof(pcm));
    cd_stream_push(pcm, CD_OUTPUT_FRAMES);
    entry(NULL);
    TEST_ASSERT_EQUAL_UINT(41, pushes);

    for (unsigned i = 0; i < pushes; ++i)
    {
        TEST_ASSERT_EQUAL_UINT8(i, observed_blocks[i]);
    }

    TEST_ASSERT_EQUAL_UINT(41, source_observed[0].blocks);
}

/**
 * @brief Verify capture is ready before source selection.
 */
static void capture_is_ready_before_source_selection(void)
{
    TEST_ASSERT_EQUAL_INT(0, cd_stream_open());
    TEST_ASSERT_FALSE(cd_stream_update(0).failed);
    TEST_ASSERT_EQUAL_UINT(0, starts);
    TEST_ASSERT_EQUAL_UINT(0, socket_opens);
    cd_stream_reset();
    cd_stream_push(pcm, CD_OUTPUT_FRAMES);
    TEST_ASSERT_FALSE(cd_stream_update(1).failed);
    entry(NULL);
    TEST_ASSERT_EQUAL_UINT(1, pushes);
    TEST_ASSERT_EQUAL_UINT8(0xa7, observed_blocks[0]);
    TEST_ASSERT_EQUAL_UINT(1, notifications);
}

/**
 * @brief Verify idle capture worker waits without network access.
 */
static void idle_capture_worker_waits_without_network_access(void)
{
    TEST_ASSERT_EQUAL_INT(0, cd_stream_open());
    entry(NULL);
    TEST_ASSERT_EQUAL_UINT(1, waits);
    TEST_ASSERT_EQUAL_UINT(0, steps);
    TEST_ASSERT_EQUAL_UINT(0, starts);
    TEST_ASSERT_EQUAL_UINT(0, socket_closes);
}

/**
 * @brief Verify overflow and reset cancel stale pcm.
 */
static void overflow_and_reset_cancel_stale_pcm(void)
{
    TEST_ASSERT_FALSE(cd_stream_update(1).failed);

    for (unsigned i = 0; i < CD_STREAM_BLOCKS + 1; ++i)
    {
        pcm[0] = (uint8_t)i;

        cd_stream_push(pcm, CD_OUTPUT_FRAMES);
    }

    TEST_ASSERT_EQUAL_UINT(0, pushes); /* Producer performs no transport work. */
    entry(NULL);
    TEST_ASSERT_EQUAL_UINT(1, resets);
    TEST_ASSERT_EQUAL_UINT(1, pushes);
    TEST_ASSERT_EQUAL_UINT(CD_STREAM_BLOCKS, observed[0]);
    TEST_ASSERT_EQUAL_INT(0, cd_stream_close());
    TEST_ASSERT_FALSE(cd_stream_update(1).failed);
    cd_stream_push(pcm, CD_OUTPUT_FRAMES);
    cd_stream_reset();

    pushes      = 0;
    sleep_count = 0;

    entry(NULL);
    TEST_ASSERT_EQUAL_UINT(0, pushes);
}

/**
 * @brief Verify failure is recovered and unselected transport stays closed.
 */
static void failure_is_recovered_and_unselected_transport_stays_closed(void)
{
    TEST_ASSERT_FALSE(cd_stream_update(1).failed);

    transport_failure = 1;

    entry(NULL);
    TEST_ASSERT_TRUE(cd_stream_update(1).failed); /* Retry cooldown after failure. */

    now += 2000;
    transport_failure = 0;

    TEST_ASSERT_FALSE(cd_stream_update(1).failed);
    TEST_ASSERT_FALSE(cd_stream_update(0).failed);

    unsigned previous = pushes;

    cd_stream_push(pcm, CD_OUTPUT_FRAMES);
    cd_stream_reset();
    cd_stream_end();
    TEST_ASSERT_EQUAL_UINT(previous, pushes);
}

/**
 * @brief Publish producer diagnostics and then reset the outgoing session.
 */
static void produce_health_and_reset(void)
{
    if (sleep_count == 1)
    {
        cd_stream_push(pcm, CD_OUTPUT_FRAMES);

        now += 60;

        cd_stream_push(pcm, CD_OUTPUT_FRAMES);
        cd_stream_activity(CD_ACTIVITY_DRIVE_WAIT | CD_ACTIVITY_SILENCE);

        now += 20;

        cd_stream_activity(CD_ACTIVITY_DRIVE_WAIT);
    }
    else if (sleep_count == 2)
    {
        cd_stream_reset();

        now += 1000;

        cd_stream_push(pcm, CD_OUTPUT_FRAMES);
        cd_stream_activity(CD_ACTIVITY_OUTPUT_BUSY);

        now += 50;

        cd_stream_activity(0);
    }
}

/**
 * @brief Verify source health distinguishes gaps and resets.
 */
static void source_health_distinguishes_gaps_and_resets(void)
{
    TEST_ASSERT_FALSE(cd_stream_update(1).failed);

    during_sleep = produce_health_and_reset;

    entry(NULL);
    TEST_ASSERT_EQUAL_UINT(2, source_observed[1].blocks);
    TEST_ASSERT_EQUAL_UINT(1, source_observed[1].gaps);
    TEST_ASSERT_EQUAL_UINT(60, source_observed[1].max_gap_ms);
    TEST_ASSERT_EQUAL_UINT(2, source_observed[1].drive_waits);
    TEST_ASSERT_EQUAL_UINT(20, source_observed[1].max_drive_wait_ms);
    TEST_ASSERT_EQUAL_UINT(1, source_observed[1].silence_blocks);
    TEST_ASSERT_EQUAL_UINT(1, source_observed[2].blocks);
    TEST_ASSERT_EQUAL_UINT(0, source_observed[2].gaps);
    TEST_ASSERT_EQUAL_UINT(0, source_observed[2].silence_blocks);
    TEST_ASSERT_EQUAL_UINT(1, source_observed[2].output_busy);
    TEST_ASSERT_EQUAL_UINT(50, source_observed[2].max_output_busy_ms);
}

/**
 * @brief Verify incompatible modules stop startup retries.
 */
static void incompatible_modules_stop_startup_retries(void)
{
    network_result = PLATFORM_NETWORK_START_RESTART_REQUIRED;

    TEST_ASSERT_TRUE(cd_stream_update(1).failed);

    now += 5000;

    TEST_ASSERT_TRUE(cd_stream_update(1).failed);
    TEST_ASSERT_EQUAL_UINT(1, starts);
    TEST_ASSERT_FALSE(cd_stream_update(0).failed);

    now += 5000;

    TEST_ASSERT_TRUE(cd_stream_update(1).failed);
    TEST_ASSERT_EQUAL_UINT(1, starts);
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(two_seconds_of_opening_audio_survive_a_delayed_sender);
    RUN_TEST(failed_startup_cleans_before_retry);
    RUN_TEST(worker_failure_retains_resources_until_cleanup);
    RUN_TEST(callbacks_copy_pcm_and_finish_on_network_worker);
    RUN_TEST(autoplay_opening_survives_slow_network_startup);
    RUN_TEST(capture_is_ready_before_source_selection);
    RUN_TEST(idle_capture_worker_waits_without_network_access);
    RUN_TEST(source_health_distinguishes_gaps_and_resets);
    RUN_TEST(overflow_and_reset_cancel_stale_pcm);
    RUN_TEST(failure_is_recovered_and_unselected_transport_stays_closed);
    RUN_TEST(incompatible_modules_stop_startup_retries);

    return UNITY_END();
}
