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
static unsigned       sleep_count;
static CdStreamSource source_observed[16];
static int            transport_failure;
static void (*during_sleep)(void);
static uint8_t pcm[CD_OUTPUT_BYTES], observed[CD_OUTPUT_BYTES];

int platform_network_startup(char* error, size_t capacity)
{
    (void)error;
    (void)capacity;
    ++starts;

    return network_result;
}

int platform_network_close(void)
{
    ++releases;

    return network_cleanup_result;
}

void platform_network_address(char* address, size_t capacity)
{
    (void)capacity;
    strcpy(address, "192.168.1.7");
}

int platform_socket_nonblocking(int fd)
{
    (void)fd;

    return 0;
}

uint32_t platform_millis(void)
{
    return now;
}

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

void platform_worker_lock(const Ps2Worker* worker)
{
    TEST_ASSERT_EQUAL_INT(1, worker->lock);
}

void platform_worker_unlock(const Ps2Worker* worker)
{
    TEST_ASSERT_EQUAL_INT(1, worker->lock);
}

void platform_worker_finish(Ps2Worker* worker)
{
    worker->running = 0;
}

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

int cd_stream_server_open(CdStreamServer* server, int (*nonblocking)(int))
{
    (void)server;
    (void)nonblocking;
    ++socket_opens;

    return server_result;
}

void cd_stream_server_overflows(CdStreamServer* server, unsigned count)
{
    (void)server;
    TEST_ASSERT_GREATER_THAN_UINT(0, count);
}

void cd_stream_server_close(CdStreamServer* server)
{
    (void)server;
    ++socket_closes;
}

void cd_stream_server_reset(CdStreamServer* server)
{
    (void)server;
    ++resets;
}

void cd_stream_server_end(CdStreamServer* server)
{
    (void)server;
    ++endings;
}

void cd_stream_server_push(CdStreamServer* server, const uint8_t* pcm)
{
    (void)server;
    ++pushes;
    memcpy(observed, pcm, sizeof(observed));
}

unsigned cd_stream_server_clients(const CdStreamServer* server)
{
    (void)server;

    return 2;
}

int cd_stream_server_step(CdStreamServer* server, uint32_t now, const char* address)
{
    (void)server;
    (void)now;
    TEST_ASSERT_EQUAL_STRING("192.168.1.7", address);
    TEST_ASSERT_LESS_THAN_UINT(16, steps);

    source_observed[steps++] = server->source;

    return transport_failure ? -1 : 0;
}

void setUp(void)
{
    network_cleanup_result = 0;
    cleanup_result         = 0;
    worker_result          = 0;
    server_result          = 0;
    network_result         = 0;
    transport_failure      = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_stream_close());

    sleep_count   = 0;
    steps         = 0;
    endings       = 0;
    resets        = 0;
    pushes        = 0;
    socket_closes = 0;
    socket_opens  = 0;
    releases      = 0;
    starts        = 0;
    during_sleep  = NULL;
    now += 2000;

    memset(pcm, 0xa7, sizeof(pcm));
    memset(observed, 0, sizeof(observed));
}

void tearDown(void)
{
    network_cleanup_result = 0;
    cleanup_result         = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_stream_close());
}

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

static void worker_failure_retains_resources_until_cleanup(void)
{
    worker_result = -1;

    TEST_ASSERT_TRUE(cd_stream_update(1).failed);

    cleanup_result = -1;

    TEST_ASSERT_EQUAL_INT(CD_STREAM_ERROR_CLOSE, cd_stream_close());
    TEST_ASSERT_EQUAL_UINT(0, releases);

    cleanup_result         = 0;
    network_cleanup_result = -1;

    TEST_ASSERT_EQUAL_INT(CD_STREAM_ERROR_CLOSE, cd_stream_close());
    TEST_ASSERT_EQUAL_UINT(1, socket_closes);

    network_cleanup_result = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_stream_close());
    TEST_ASSERT_EQUAL_UINT(1, socket_closes); /* Completed cleanup is not repeated. */
}

static void produce_and_finish(void)
{
    if (sleep_count == 1)
    {
        cd_stream_push(pcm, CD_OUTPUT_FRAMES);
        cd_stream_end();
        memset(pcm, 0x55, sizeof(pcm)); /* Copy lifetime must not borrow producer memory. */
    }
}

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

static void failure_is_recovered_and_disabled_callbacks_do_nothing(void)
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(failed_startup_cleans_before_retry);
    RUN_TEST(worker_failure_retains_resources_until_cleanup);
    RUN_TEST(callbacks_copy_pcm_and_finish_on_network_worker);
    RUN_TEST(source_health_distinguishes_gaps_and_resets);
    RUN_TEST(overflow_and_reset_cancel_stale_pcm);
    RUN_TEST(failure_is_recovered_and_disabled_callbacks_do_nothing);
    RUN_TEST(incompatible_modules_stop_startup_retries);

    return UNITY_END();
}
