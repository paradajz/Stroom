/* Run the real reader and DMA/ring adapter concurrently. Only SDK operations
 * and EE worker primitives are substituted. A blocked SDK call must never
 * prevent the playback thread from draining the reserve or requesting a seek. */
#include "platform/cd/reader.h"
#include "platform/thread/worker.h"
#include "platform/iop/modules.h"
#include "platform/time/clock.h"
#include <libcdvd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include "unity.h"

#define REFILL_SECTORS ((PS2_CD_RESERVE_SECTORS / 16) * 16)

static pthread_t       thread;
static pthread_mutex_t mailbox   = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t device    = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  condition = PTHREAD_COND_INITIALIZER;
static void (*entry)(void*);
static void*        argument;
static int          launched, exited;
static int          block_kind, entered, released, reject_read, read_error, tray_event, disc_type;
static int          start_failure, deletion_failure;
static atomic_uint  clock_offset;
static Ps2CdReader  reader;
static volatile int running = 1;
static char         error_text[128];

static void pause_ms(unsigned milliseconds)
{
    struct timespec delay = { milliseconds / 1000, (long)(milliseconds % 1000) * 1000000 };

    nanosleep(&delay, NULL);
}

uint32_t platform_millis(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);

    return (uint32_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000) + atomic_load(&clock_offset);
}

void platform_sleep_us(uint32_t microseconds)
{
    struct timespec delay = { 0, (long)microseconds * 1000 };

    nanosleep(&delay, NULL);
}

static void* run(void* unused)
{
    (void)unused;
    entry(argument);

    return NULL;
}

int platform_worker_open(Ps2Worker* worker, const Ps2WorkerConfig* config, Ps2WorkerError* error)
{
    (void)error;

    if (start_failure)
    {
        return -1;
    }

    entry           = config->entry;
    argument        = config->argument;
    worker->thread  = 1;
    worker->done    = 1;
    worker->lock    = 1;
    worker->running = 1;
    launched        = 1;

    return pthread_create(&thread, NULL, run, NULL);
}

void platform_worker_lock(const Ps2Worker* worker)
{
    (void)worker;
    pthread_mutex_lock(&mailbox);
}

void platform_worker_unlock(const Ps2Worker* worker)
{
    (void)worker;
    pthread_mutex_unlock(&mailbox);
}

void platform_worker_finish(Ps2Worker* worker)
{
    pthread_mutex_lock(&mailbox);

    worker->running = 0;
    exited          = 1;

    pthread_mutex_unlock(&mailbox);
}

int platform_worker_close(Ps2Worker* worker)
{
    pthread_mutex_lock(&mailbox);

    worker->running = 0;
    int complete    = exited;

    pthread_mutex_unlock(&mailbox);

    if (launched && !complete)
    {
        return 1;
    }

    if (launched)
    {
        pthread_join(thread, NULL);

        launched = 0;
    }

    if (deletion_failure)
    {
        return -1;
    }

    worker->thread = -1;
    worker->done   = -1;
    worker->lock   = -1;

    return 0;
}

/* 1 = read submission, 2 = completion error query, 3 = media probe. */
static void device_gate(int kind)
{
    pthread_mutex_lock(&device);

    if (block_kind == kind)
    {
        entered = 1;

        pthread_cond_broadcast(&condition);

        while (!released)
        {
            pthread_cond_wait(&condition, &device);
        }

        block_kind = 0;
    }

    pthread_mutex_unlock(&device);
}

static void arm_block(int kind)
{
    pthread_mutex_lock(&device);

    block_kind = kind;
    released   = 0;
    entered    = 0;

    pthread_mutex_unlock(&device);
}

static void await_block(void)
{
    struct timespec deadline;

    clock_gettime(CLOCK_REALTIME, &deadline);

    deadline.tv_sec += 2;

    pthread_mutex_lock(&device);

    int result = 0;

    while (!entered && !result)
    {
        result = pthread_cond_timedwait(&condition, &device, &deadline);
    }

    int blocked = entered;

    pthread_mutex_unlock(&device);
    TEST_ASSERT_TRUE_MESSAGE(blocked, "Reader must reach the selected SDK operation");
}

static void release_block(void)
{
    pthread_mutex_lock(&device);

    released = 1;

    pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&device);
}

int sceCdInit(int mode)
{
    (void)mode;

    return 1;
}

int sceCdTrayReq(int mode, u32* changed)
{
    (void)mode;
    device_gate(3);
    pthread_mutex_lock(&device);

    *changed   = tray_event;
    tray_event = 0;

    pthread_mutex_unlock(&device);

    return 1;
}

int sceCdGetDiskType(void)
{
    pthread_mutex_lock(&device);

    int type = disc_type;

    pthread_mutex_unlock(&device);

    return type;
}

int sceCdGetToc(void* bytes)
{
    memset(bytes, 0x5a, 2064);

    return 1;
}

int sceCdGetError(void)
{
    device_gate(2);
    pthread_mutex_lock(&device);

    int result = read_error;
    read_error = SCECdErNO;

    pthread_mutex_unlock(&device);

    return result;
}

int sceCdBreak(void)
{
    return 1;
}

int sceCdSync(int mode)
{
    (void)mode;

    return 0;
}

int sceCdReadCDDA(int start, int count, void* bytes, sceCdRMode* mode)
{
    (void)mode;
    device_gate(1);
    pthread_mutex_lock(&device);

    int reject  = reject_read;
    reject_read = 0;

    pthread_mutex_unlock(&device);

    if (reject)
    {
        return 0;
    }

    for (int i = 0; i < count; ++i)
    {
        uint32_t sector = (uint32_t)(start + i);

        memcpy((uint8_t*)bytes + i * CD_SECTOR_BYTES, &sector, sizeof(sector));
        memcpy((uint8_t*)bytes + (i + 1) * CD_SECTOR_BYTES - sizeof(sector), &sector, sizeof(sector));
    }

    return 1;
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

static void stage(void* context, const char* message)
{
    (void)context;
    (void)message;
}

static const Ps2CdDriveRuntime runtime = { .running = &running, .stage = stage, .error = error_text, .capacity = sizeof(error_text) };

static void await_buffer(unsigned target)
{
    uint32_t deadline = platform_millis();

    while (platform_cd_reader_buffered(&reader) < target && (uint32_t)(platform_millis() - deadline) < 2000)
    {
        pause_ms(1);
    }

    TEST_ASSERT_EQUAL_UINT(target, platform_cd_reader_buffered(&reader));
}

static void consume_range(unsigned start, unsigned sectors)
{
    for (unsigned i = 0; i < sectors; ++i)
    {
        const uint8_t* bytes = platform_cd_reader_sector(&reader);

        TEST_ASSERT_NOT_NULL(bytes);

        uint32_t first, last;

        memcpy(&first, bytes, sizeof(first));
        memcpy(&last, bytes + CD_SECTOR_BYTES - sizeof(last), sizeof(last));
        TEST_ASSERT_EQUAL_UINT(start + i, first);
        TEST_ASSERT_EQUAL_UINT(start + i, last);
        platform_cd_reader_consume(&reader);
    }
}

void setUp(void)
{
    deletion_failure = 0;
    start_failure    = 0;
    exited           = 0;
    launched         = 0;
    tray_event       = 0;
    reject_read      = 0;
    released         = 0;
    entered          = 0;
    block_kind       = 0;
    read_error       = SCECdErNO;
    disc_type        = SCECdCDDA;

    atomic_store(&clock_offset, 0);
    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_open(&reader, &runtime));

    Ps2CdMedia observed;
    uint32_t   started = platform_millis();

    while (platform_cd_reader_media(&observed) && (uint32_t)(platform_millis() - started) < 2000)
    {
        pause_ms(1);
    }

    TEST_ASSERT_TRUE(observed.audio);
    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_reset(&reader, 0));
    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_prefetch(&reader, 20000));
    await_buffer(PS2_CD_RESERVE_SECTORS);
}

void tearDown(void)
{
    release_block();

    deletion_failure = 0;
    start_failure    = 0;
    uint32_t started = platform_millis();
    int      result;

    do
    {
        result = platform_cd_reader_close(&reader);

        if (result)
        {
            pause_ms(1);
        }
    } while (result && (uint32_t)(platform_millis() - started) < 2000);
    TEST_ASSERT_EQUAL_INT(0, result);
}

static void blocked_read_does_not_block_playback(void)
{
    arm_block(1);
    consume_range(0, 16);
    await_block();
    TEST_ASSERT_EQUAL_UINT(PS2_CD_RESERVE_SECTORS - 16, platform_cd_reader_buffered(&reader));
    consume_range(16, PS2_CD_RESERVE_SECTORS - 16);
    TEST_ASSERT_NULL(platform_cd_reader_sector(&reader));
    release_block();
    await_buffer(REFILL_SECTORS);
    consume_range(PS2_CD_RESERVE_SECTORS, REFILL_SECTORS);
}

static void blocked_error_query_does_not_block_playback(void)
{
    arm_block(2);
    consume_range(0, 16);
    await_block();
    consume_range(16, PS2_CD_RESERVE_SECTORS - 16);
    release_block();
    await_buffer(REFILL_SECTORS);
    consume_range(PS2_CD_RESERVE_SECTORS, REFILL_SECTORS);
}

static void blocked_media_probe_does_not_block_playback(void)
{
    arm_block(3);
    atomic_fetch_add(&clock_offset, 501);
    await_block();
    consume_range(0, PS2_CD_RESERVE_SECTORS);
    release_block();
    await_buffer(REFILL_SECTORS);
    consume_range(PS2_CD_RESERVE_SECTORS, REFILL_SECTORS);
}

static void seek_discards_completion_from_blocked_read(void)
{
    arm_block(1);
    consume_range(0, 16);
    await_block();
    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_reset(&reader, 1000));
    TEST_ASSERT_NULL(platform_cd_reader_sector(&reader));
    /* Replace a seek while the previous request is still in the SDK. */
    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_reset(&reader, 2000));
    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_prefetch(&reader, 2100));
    release_block();
    await_buffer(100);
    consume_range(2000, 100);
}

static void close_retains_blocked_worker_until_exit(void)
{
    arm_block(1);
    consume_range(0, 16);
    await_block();
    TEST_ASSERT_EQUAL_INT(1, platform_cd_reader_close(&reader));
    TEST_ASSERT_EQUAL_INT(PS2_CD_DRIVE_ERROR_WORKER_START, platform_cd_reader_open(&reader, &runtime));
    release_block();

    uint32_t started = platform_millis();
    int      result;

    do
    {
        result = platform_cd_reader_close(&reader);

        pause_ms(1);
    } while (result && (uint32_t)(platform_millis() - started) < 2000);
    TEST_ASSERT_EQUAL_INT(0, result);

    exited = 0;

    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_open(&reader, &runtime));
}

static void recovered_read_preserves_order(void)
{
    arm_block(2);
    consume_range(0, 16);
    await_block();
    pthread_mutex_lock(&device);

    read_error = 1;

    pthread_mutex_unlock(&device);
    release_block();
    consume_range(16, PS2_CD_RESERVE_SECTORS - 16);
    await_buffer(REFILL_SECTORS);
    consume_range(PS2_CD_RESERVE_SECTORS, REFILL_SECTORS);
}

static void media_change_invalidates_reserve(void)
{
    pthread_mutex_lock(&device);

    tray_event = 1;
    disc_type  = SCECdNODISC;

    pthread_mutex_unlock(&device);
    atomic_fetch_add(&clock_offset, 501);

    Ps2CdMedia observed;
    uint32_t   started = platform_millis();

    do
    {
        platform_cd_reader_media(&observed);

        if (observed.absent)
        {
            break;
        }

        pause_ms(1);
    } while ((uint32_t)(platform_millis() - started) < 2000);
    TEST_ASSERT_TRUE(observed.absent);
    TEST_ASSERT_TRUE(observed.changed);
    TEST_ASSERT_EQUAL_UINT(0, platform_cd_reader_buffered(&reader));
    TEST_ASSERT_NULL(platform_cd_reader_sector(&reader));
    pause_ms(5);
    TEST_ASSERT_EQUAL_UINT(0, platform_cd_reader_buffered(&reader));
}

static void toc_request_does_not_block_playback(void)
{
    arm_block(2);

    Ps2CdToc toc;

    TEST_ASSERT_EQUAL_INT(1, platform_cd_reader_toc(&toc));
    await_block();
    consume_range(0, PS2_CD_RESERVE_SECTORS);
    release_block();

    uint32_t started = platform_millis();
    int      result;

    do
    {
        result = platform_cd_reader_toc(&toc);

        pause_ms(1);
    } while (result > 0 && (uint32_t)(platform_millis() - started) < 2000);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_UINT(2064, toc.size);
    TEST_ASSERT_EQUAL_UINT8(0x5a, toc.data[0]);
}

static void concurrent_refill_preserves_order_across_wraps(void)
{
    uint32_t started = platform_millis();

    for (unsigned sector = 0; sector < 3000; ++sector)
    {
        while (!platform_cd_reader_sector(&reader) && (uint32_t)(platform_millis() - started) < 2000)
        {
            pause_ms(1);
        }

        consume_range(sector, 1);
    }
}

static void rejected_submission_retries_exact_sector(void)
{
    arm_block(1);
    consume_range(0, 16);
    await_block();
    pthread_mutex_lock(&device);

    reject_read = 1;

    pthread_mutex_unlock(&device);
    release_block();
    consume_range(16, PS2_CD_RESERVE_SECTORS - 16);
    await_buffer(REFILL_SECTORS);
    consume_range(PS2_CD_RESERVE_SECTORS, REFILL_SECTORS);
}

static void deletion_failure_retains_resources_for_retry(void)
{
    deletion_failure = 1;
    uint32_t started = platform_millis();
    int      result;

    do
    {
        result = platform_cd_reader_close(&reader);

        pause_ms(1);
    } while (result > 0 && (uint32_t)(platform_millis() - started) < 2000);
    TEST_ASSERT_LESS_THAN_INT(0, result);
    TEST_ASSERT_EQUAL_INT(PS2_CD_DRIVE_ERROR_WORKER_START, platform_cd_reader_open(&reader, &runtime));

    deletion_failure = 0;

    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_close(&reader));

    exited = 0;

    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_open(&reader, &runtime));
}

static void worker_start_failure_allows_cleanup_and_reopen(void)
{
    uint32_t started = platform_millis();
    int      result;

    do
    {
        result = platform_cd_reader_close(&reader);

        pause_ms(1);
    } while (result && (uint32_t)(platform_millis() - started) < 2000);
    TEST_ASSERT_EQUAL_INT(0, result);

    start_failure = 1;

    TEST_ASSERT_EQUAL_INT(PS2_CD_DRIVE_ERROR_WORKER_START, platform_cd_reader_open(&reader, &runtime));
    TEST_ASSERT_EQUAL_STRING("CD READER WORKER START FAILED", error_text);
    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_close(&reader));

    exited        = 0;
    start_failure = 0;

    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_open(&reader, &runtime));
}

static void suspended_reader_does_not_refill_until_requested(void)
{
    platform_cd_reader_suspend(&reader);
    consume_range(0, PS2_CD_RESERVE_SECTORS);
    pause_ms(10);
    TEST_ASSERT_EQUAL_UINT(0, platform_cd_reader_buffered(&reader));
    TEST_ASSERT_EQUAL_INT(0, platform_cd_reader_prefetch(&reader, 20000));
    await_buffer(REFILL_SECTORS);
    consume_range(PS2_CD_RESERVE_SECTORS, REFILL_SECTORS);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(blocked_read_does_not_block_playback);
    RUN_TEST(blocked_error_query_does_not_block_playback);
    RUN_TEST(blocked_media_probe_does_not_block_playback);
    RUN_TEST(seek_discards_completion_from_blocked_read);
    RUN_TEST(close_retains_blocked_worker_until_exit);
    RUN_TEST(recovered_read_preserves_order);
    RUN_TEST(media_change_invalidates_reserve);
    RUN_TEST(toc_request_does_not_block_playback);
    RUN_TEST(concurrent_refill_preserves_order_across_wraps);
    RUN_TEST(rejected_submission_retries_exact_sector);
    RUN_TEST(deletion_failure_retains_resources_for_retry);
    RUN_TEST(worker_start_failure_allows_cleanup_and_reopen);
    RUN_TEST(suspended_reader_does_not_refill_until_requested);

    return UNITY_END();
}
