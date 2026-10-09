#include "ui/artwork/worker.h"
#include "unity.h"
#include <kernel.h>
#include "platform/time/sleep.h"
#include "ui/presentation.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void* _gp;

static uint32_t clock_ms;
static int      delay_calls, delay_us;

static struct
{
    pthread_mutex_t lock;
    pthread_cond_t  changed;
    int             count;
} semaphores[3];

static pthread_t       decoder_thread;
static ee_thread_t     thread_configuration;
static int             semaphores_created, thread_created, thread_started, fail_operation, operations, caller_priority;
static pthread_mutex_t gate    = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  changed = PTHREAD_COND_INITIALIZER;
static unsigned        entered, allowed;
static int             wrong_thread, exit_reached, exit_allowed, thread_exited;
static pthread_t       caller_thread;
static int             status_error;

int GetThreadId(void)
{
    return 1;
}

int ReferThreadStatus(int id, ee_thread_status_t* status)
{
    if (id == 4 && status_error)
    {
        return -1;
    }

    status->current_priority = caller_priority;

    pthread_mutex_lock(&gate);

    status->status = id == 4 && thread_exited ? THS_DORMANT : THS_READY;

    pthread_mutex_unlock(&gate);

    return 0;
}

int CreateSema(ee_sema_t* config)
{
    if (++operations == fail_operation)
    {
        return -1;
    }

    int id = semaphores_created++;

    pthread_mutex_init(&semaphores[id].lock, NULL);
    pthread_cond_init(&semaphores[id].changed, NULL);

    semaphores[id].count = config->init_count;

    return id;
}

int DeleteSema(int id)
{
    pthread_mutex_destroy(&semaphores[id].lock);
    pthread_cond_destroy(&semaphores[id].changed);
    --semaphores_created;

    return 0;
}

int WaitSema(int id)
{
    pthread_mutex_lock(&semaphores[id].lock);

    while (!semaphores[id].count)
    {
        pthread_cond_wait(&semaphores[id].changed, &semaphores[id].lock);
    }

    --semaphores[id].count;
    pthread_mutex_unlock(&semaphores[id].lock);

    return 0;
}

int SignalSema(int id)
{
    pthread_mutex_lock(&semaphores[id].lock);

    semaphores[id].count = 1;

    pthread_cond_signal(&semaphores[id].changed);
    pthread_mutex_unlock(&semaphores[id].lock);

    return 0;
}

int CreateThread(ee_thread_t* config)
{
    if (++operations == fail_operation)
    {
        return -1;
    }

    thread_configuration = *config;
    thread_created       = 1;

    return 4;
}

static void* start(void* argument)
{
    thread_configuration.func(argument);

    return NULL;
}

int StartThread(int id, void* argument)
{
    (void)id;

    if (++operations == fail_operation)
    {
        return -1;
    }

    thread_exited = exit_reached = 0;
    thread_started               = pthread_create(&decoder_thread, NULL, start, argument) == 0;

    return thread_started ? 0 : -1;
}

int DeleteThread(int id)
{
    (void)id;

    if (thread_started)
    {
        TEST_ASSERT_TRUE(thread_exited);
        pthread_join(decoder_thread, NULL);
    }

    thread_created = thread_started = 0;

    return 0;
}

void ExitThread(void)
{
    pthread_mutex_lock(&gate);

    exit_reached = 1;

    pthread_cond_broadcast(&changed);

    while (!exit_allowed)
    {
        pthread_cond_wait(&changed, &gate);
    }

    thread_exited = 1;

    pthread_mutex_unlock(&gate);
    pthread_exit(NULL);
}

uint32_t platform_millis(void)
{
    return clock_ms;
}

void platform_sleep_us(uint32_t microseconds)
{
    ++delay_calls;

    delay_us = microseconds;

    const struct timespec delay = { .tv_nsec = 1000000 };

    nanosleep(&delay, NULL);
}

int artwork_decode(const void* bytes, size_t size, ArtworkImage* image)
{
    (void)size;
    pthread_mutex_lock(&gate);

    wrong_thread |= pthread_equal(caller_thread, pthread_self());

    unsigned this_job = ++entered;

    pthread_cond_broadcast(&changed);

    while (allowed < this_job)
    {
        pthread_cond_wait(&changed, &gate);
    }

    pthread_mutex_unlock(&gate);

    unsigned value = *(const unsigned char*)bytes;

    image->width = image->height = value;
    image->pixels[0]             = value;

    return (value != 4) ? 0 : -1;
}

static void wait_entered(unsigned jobs)
{
    pthread_mutex_lock(&gate);

    while (entered < jobs)
    {
        pthread_cond_wait(&changed, &gate);
    }

    pthread_mutex_unlock(&gate);
}

static void release(unsigned jobs)
{
    pthread_mutex_lock(&gate);

    allowed = jobs;

    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
}

static ArtworkBlob blob(unsigned value)
{
    ArtworkBlob result = { .size = 1 };

    result.data = malloc(1);

    TEST_ASSERT_NOT_NULL(result.data);

    result.data[0] = (uint8_t)value;

    snprintf(result.metadata.artwork_url, sizeof(result.metadata.artwork_url), "http://1.2.3.4/%u", value);
    snprintf(result.metadata.title, sizeof(result.metadata.title), "Cover %u", value);

    return result;
}

static void wait_result(const TrackMetadata* metadata, ArtworkImage* image, int* valid)
{
    const struct timespec delay = { .tv_nsec = 1000000 };

    for (unsigned i = 0; i < 1000; ++i)
    {
        if (artwork_worker_take(metadata, image, valid, ARTWORK_PER_URL) == 0)
        {
            return;
        }

        nanosleep(&delay, NULL);
    }

    TEST_FAIL_MESSAGE("decoder did not publish matching result");
}

void setUp(void)
{
    operations = fail_operation = 0;
    status_error                = 0;
    caller_priority             = 0x61;
    entered = allowed = 0;
    wrong_thread = delay_calls = 0;
    clock_ms                   = 0;
    exit_reached = thread_exited = 0;
    exit_allowed                 = 1;
    caller_thread                = pthread_self();
}

static void close_worker(void)
{
    const struct timespec delay = { .tv_nsec = 1000000 };

    for (unsigned i = 0; i < 1000; ++i)
    {
        if (artwork_worker_close() == 0)
        {
            return;
        }

        nanosleep(&delay, NULL);
    }

    TEST_FAIL_MESSAGE("worker did not finish cleanup");
}

void tearDown(void)
{
    release(100);
    close_worker();
    TEST_ASSERT_EQUAL_INT(0, semaphores_created);
    TEST_ASSERT_EQUAL_INT(0, thread_created);
}

/** @brief A blocked codec never blocks submissions, polling, or replacement of a pending cover. */
static void asynchronous_latest_cover(void)
{
    TEST_ASSERT_TRUE(artwork_worker_open() == 0);
    TEST_ASSERT_TRUE(artwork_worker_open() == 0);
    artwork_worker_yield(0);
    TEST_ASSERT_EQUAL_INT(0, delay_calls);
    TEST_ASSERT_EQUAL_INT(0x62, thread_configuration.initial_priority);

    ArtworkBlob   first = blob(1), replaced = blob(2), latest = blob(3);
    TrackMetadata latest_metadata = latest.metadata;

    strcpy(latest_metadata.title, "Updated title, same artwork");
    TEST_ASSERT_TRUE(artwork_worker_submit(&first) == 0);
    TEST_ASSERT_NULL(first.data);
    wait_entered(1);

    clock_ms = UINT32_MAX - 1;

    artwork_worker_yield(UINT32_MAX - 5);
    TEST_ASSERT_EQUAL_INT(1, delay_calls);
    TEST_ASSERT_EQUAL_INT(8000, delay_us);
    artwork_worker_yield(clock_ms - 16);

    unsigned budget = ui_background_work_budget(16);

    TEST_ASSERT_EQUAL_INT(budget ? 2 : 1, delay_calls);

    if (budget)
    {
        TEST_ASSERT_EQUAL_INT(budget * 1000, delay_us);
    }

    TEST_ASSERT_TRUE(artwork_worker_submit(&replaced) == 0);
    TEST_ASSERT_TRUE(artwork_worker_submit(&latest) == 0);
    TEST_ASSERT_NULL(replaced.data);
    TEST_ASSERT_NULL(latest.data);

    static ArtworkImage image;

    image.pixels[0] = 99;

    int valid = -1;

    TEST_ASSERT_TRUE(!(artwork_worker_take(&latest_metadata, &image, &valid, ARTWORK_PER_URL) == 0));
    release(1);
    /* Polling discards the stale completion and permits the latest pending job. */
    const struct timespec delay = { .tv_nsec = 1000000 };

    for (unsigned i = 0; i < 1000; ++i)
    {
        TEST_ASSERT_TRUE(!(artwork_worker_take(&latest_metadata, &image, &valid, ARTWORK_PER_URL) == 0));
        pthread_mutex_lock(&gate);

        unsigned jobs = entered;

        pthread_mutex_unlock(&gate);

        if (jobs == 2)
        {
            break;
        }

        nanosleep(&delay, NULL);
    }

    wait_entered(2);
    TEST_ASSERT_EQUAL_UINT(99, image.pixels[0]);
    release(2);
    wait_result(&latest_metadata, &image, &valid);
    TEST_ASSERT_TRUE(valid);
    TEST_ASSERT_EQUAL_UINT(3, image.pixels[0]);
    TEST_ASSERT_EQUAL_UINT(3, image.width);
    TEST_ASSERT_TRUE(!(artwork_worker_take(&latest_metadata, &image, &valid, ARTWORK_PER_URL) == 0));
    TEST_ASSERT_FALSE(wrong_thread);

    ArtworkBlob   invalid          = blob(4);
    TrackMetadata invalid_metadata = invalid.metadata;

    TEST_ASSERT_TRUE(artwork_worker_submit(&invalid) == 0);
    release(3);
    wait_result(&invalid_metadata, &image, &valid);
    TEST_ASSERT_FALSE(valid);
    close_worker();
    TEST_ASSERT_TRUE(artwork_worker_open() == 0);
    TEST_ASSERT_TRUE(!(artwork_worker_take(&latest_metadata, &image, &valid, ARTWORK_PER_URL) == 0));
}

/** @brief Every startup failure releases created objects and leaves the caller owning input. */
static void startup_failures(void)
{
    for (int failure = 1; failure <= 5; ++failure)
    {
        operations     = 0;
        fail_operation = failure;

        TEST_ASSERT_TRUE(!(artwork_worker_open() == 0));
        TEST_ASSERT_EQUAL_INT(0, semaphores_created);
        TEST_ASSERT_EQUAL_INT(0, thread_created);

        ArtworkBlob job = blob(1);

        TEST_ASSERT_TRUE(!(artwork_worker_submit(&job) == 0));
        TEST_ASSERT_NOT_NULL(job.data);
        free(job.data);
    }

    fail_operation  = 0;
    caller_priority = MAX_PRIORITY - 1;

    TEST_ASSERT_TRUE(!(artwork_worker_open() == 0));
}

/** @brief Shutdown remains pending during decoding and until the kernel confirms exit. */
static void shutdown_during_decode(void)
{
    TEST_ASSERT_TRUE(artwork_worker_open() == 0);

    ArtworkBlob active = blob(1), queued = blob(2);

    TEST_ASSERT_TRUE(artwork_worker_submit(&active) == 0);
    wait_entered(1);
    TEST_ASSERT_TRUE(artwork_worker_submit(&queued) == 0);
    pthread_mutex_lock(&gate);

    exit_allowed = 0;

    pthread_mutex_unlock(&gate);
    TEST_ASSERT_TRUE(!(artwork_worker_close() == 0));
    TEST_ASSERT_EQUAL_INT(1, thread_started);
    TEST_ASSERT_EQUAL_INT(3, semaphores_created);
    TEST_ASSERT_TRUE(!(artwork_worker_open() == 0));
    release(1);
    pthread_mutex_lock(&gate);

    while (!exit_reached)
    {
        pthread_cond_wait(&changed, &gate);
    }

    TEST_ASSERT_EQUAL_INT(1, thread_started);
    TEST_ASSERT_EQUAL_INT(3, semaphores_created);
    pthread_mutex_unlock(&gate);
    TEST_ASSERT_TRUE(!(artwork_worker_close() == 0));
    TEST_ASSERT_TRUE(!(artwork_worker_open() == 0));
    TEST_ASSERT_EQUAL_INT(3, semaphores_created);
    pthread_mutex_lock(&gate);

    exit_allowed = 1;

    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    close_worker();
    TEST_ASSERT_EQUAL_UINT(1, entered);
    TEST_ASSERT_EQUAL_INT(0, semaphores_created);
    TEST_ASSERT_EQUAL_INT(0, thread_created);
}

static void failed_close_can_retry(void)
{
    TEST_ASSERT_TRUE(artwork_worker_open() == 0);

    ArtworkBlob active = blob(1);

    TEST_ASSERT_TRUE(artwork_worker_submit(&active) == 0);
    wait_entered(1);

    status_error = 1;

    TEST_ASSERT_TRUE(!(artwork_worker_close() == 0));
    TEST_ASSERT_EQUAL_INT(3, semaphores_created);
    TEST_ASSERT_EQUAL_INT(1, thread_created);
    TEST_ASSERT_TRUE(!(artwork_worker_open() == 0));
    release(1);

    status_error = 0;

    close_worker();
    TEST_ASSERT_EQUAL_INT(0, semaphores_created);
    TEST_ASSERT_EQUAL_INT(0, thread_created);
    TEST_ASSERT_TRUE(artwork_worker_open() == 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(asynchronous_latest_cover);
    RUN_TEST(startup_failures);
    RUN_TEST(failed_close_can_retry);
    RUN_TEST(shutdown_during_decode);

    return UNITY_END();
}

int ChangeThreadPriority(int id, int priority)
{
    (void)id;
    (void)priority;
    TEST_FAIL_MESSAGE("Decoder setup must not change renderer priority");

    return -1;
}
