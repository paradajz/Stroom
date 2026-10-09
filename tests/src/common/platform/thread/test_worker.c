#include "platform/thread/worker.h"
#include "util/diagnostics.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include "unity.h"
#include <unistd.h>

int                  _gp;
static Ps2Worker     instance;
static unsigned char stack[1024] __attribute__((aligned(16)));
static int           operation, fail_at, live_semaphores, live_threads;
static int           status_calls, fail_status_at, dormant_at, waits, sleeps, deleted;
static int           delete_error, sema_delete_error;
static int           wake_token;
static char          diagnostics[2048];
static unsigned      diagnostic_used;

int __wrap_printf(const char* format, ...)
{
    va_list arguments;

    va_start(arguments, format);

    int size = vsnprintf(diagnostics + diagnostic_used, sizeof(diagnostics) - diagnostic_used, format, arguments);

    va_end(arguments);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, size);
    TEST_ASSERT_LESS_THAN_UINT(sizeof(diagnostics) - diagnostic_used, (unsigned)size);

    diagnostic_used += (unsigned)size;

    return size;
}

static void entry(void* argument)
{
    (void)argument;
}

static const Ps2WorkerConfig config = { .entry = entry, .argument = &instance, .stack = stack, .stack_bytes = sizeof(stack), .priority = 0x60 };

int CreateSema(ee_sema_t* sema)
{
    TEST_ASSERT_EQUAL_INT(1, sema->max_count);
    ++operation;

    if (operation == fail_at)
    {
        return -7;
    }

    ++live_semaphores;

    return operation;
}

int DeleteSema(int id)
{
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, id);
    TEST_ASSERT_EQUAL_INT(0, live_threads);

    if (id == sema_delete_error)
    {
        return -1;
    }

    --live_semaphores;

    return 0;
}

int CreateThread(ee_thread_t* thread)
{
    TEST_ASSERT_EQUAL_PTR(stack, thread->stack);
    TEST_ASSERT_EQUAL_PTR(&_gp, thread->gp_reg);
    TEST_ASSERT_EQUAL_INT(sizeof(stack), thread->stack_size);
    TEST_ASSERT_EQUAL_INT(config.priority, thread->initial_priority);
    TEST_ASSERT_TRUE(thread->func == entry);

    if (++operation == fail_at)
    {
        return -7;
    }

    ++live_threads;

    return 3;
}

int StartThread(int id, void* arg)
{
    TEST_ASSERT_EQUAL_INT(3, id);
    TEST_ASSERT_EQUAL_PTR(&instance, arg);
    TEST_ASSERT_TRUE(instance.running);

    return ++operation == fail_at ? -7 : 0;
}

int DeleteThread(int id)
{
    TEST_ASSERT_EQUAL_INT(3, id);

    if (delete_error)
    {
        return -1;
    }

    if (fail_at != 4)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(dormant_at, status_calls);
    }

    --live_threads;
    ++deleted;

    return 0;
}

int ReferThreadStatus(int id, ee_thread_status_t* status)
{
    TEST_ASSERT_EQUAL_INT(3, id);

    if (++status_calls == fail_status_at)
    {
        return -1;
    }

    status->status = status_calls >= dormant_at ? THS_DORMANT : 1;

    return 0;
}

int WaitSema(int id)
{
    if (id == instance.wake)
    {
        TEST_ASSERT_EQUAL_INT(1, wake_token);

        wake_token = 0;

        return 0;
    }

    TEST_ASSERT_EQUAL_INT(instance.done, id);
    TEST_ASSERT_FALSE(instance.running);

    if (instance.wake >= 0)
    {
        TEST_ASSERT_EQUAL_INT(1, wake_token);
    }

    ++waits;

    TEST_FAIL_MESSAGE("Close must not wait for worker completion");

    return -1;
}

int SignalSema(int id)
{
    if (id == instance.wake)
    {
        if (wake_token)
        {
            return -1;
        }

        wake_token = 1;

        return 0;
    }

    TEST_ASSERT_EQUAL_INT(instance.done, id);

    return 0;
}

void ExitThread(void)
{}

int usleep(useconds_t duration)
{
    TEST_ASSERT_EQUAL_INT(1000, duration);
    TEST_ASSERT_EQUAL_INT(1, live_threads);
    TEST_ASSERT_EQUAL_INT(instance.wake >= 0 ? 3 : 2, live_semaphores);
    ++sleeps;

    return 0;
}

void setUp(void)
{
    diagnostics[0]  = 0;
    diagnostic_used = 0;
    instance        = (Ps2Worker)PS2_WORKER_INITIALIZER;
    operation = fail_at = live_semaphores = live_threads = 0;
    status_calls = fail_status_at = waits = sleeps = deleted = delete_error = 0;
    dormant_at                                                              = 1;
    wake_token                                                              = 0;
    sema_delete_error                                                       = -1;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT(0, live_threads);
    TEST_ASSERT_EQUAL_INT(0, live_semaphores);
}

/** @brief Every startup failure releases precisely the resources already created. */
static void startup_failures(void)
{
    const char* stages[]   = { "WORKER LOCK", "WORKER DONE", "THREAD CREATE", "THREAD START" };
    const int   failures[] = { PS2_WORKER_ERROR_LOCK_CREATE, PS2_WORKER_ERROR_DONE_CREATE, PS2_WORKER_ERROR_THREAD_CREATE, PS2_WORKER_ERROR_THREAD_START };

    for (int i = 1; i <= 4; ++i)
    {
        operation = 0;
        fail_at   = i;
        Ps2WorkerError error;

        TEST_ASSERT_EQUAL_INT(failures[i - 1], platform_worker_open(&instance, &config, &error));
        TEST_ASSERT_EQUAL_STRING(stages[i - 1], error.stage);
        TEST_ASSERT_EQUAL_INT(-7, error.code);
        TEST_ASSERT_FALSE(instance.running);
        TEST_ASSERT_EQUAL_INT(-1, instance.thread);
        TEST_ASSERT_EQUAL_INT(-1, instance.lock);
        TEST_ASSERT_EQUAL_INT(-1, instance.done);
        TEST_ASSERT_EQUAL_INT(0, live_threads);
        TEST_ASSERT_EQUAL_INT(0, live_semaphores);
        TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
    }
}

/** @brief Completion does not allow deletion until the thread actually exits. */
static void pending_close_preserves_resources(void)
{
    TEST_ASSERT_TRUE(platform_worker_open(&instance, &config, NULL) == 0);

    dormant_at = 4;

    platform_worker_finish(&instance);

    for (int i = 1; i < dormant_at; ++i)
    {
        TEST_ASSERT_EQUAL_INT(1, platform_worker_close(&instance));
        TEST_ASSERT_EQUAL_INT(i, status_calls);
        TEST_ASSERT_EQUAL_INT(1, live_threads);
        TEST_ASSERT_EQUAL_INT(2, live_semaphores);
        TEST_ASSERT_EQUAL_INT(0, waits);
        TEST_ASSERT_EQUAL_INT(0, sleeps);
        TEST_ASSERT_EQUAL_STRING("", diagnostics);

        Ps2Worker saved   = instance;
        int       created = operation;

        TEST_ASSERT_TRUE(!(platform_worker_open(&instance, &config, NULL) == 0));
        TEST_ASSERT_EQUAL_MEMORY(&saved, &instance, sizeof(instance));
        TEST_ASSERT_EQUAL_INT(created, operation);
    }

    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
    TEST_ASSERT_EQUAL_INT(1, deleted);
    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
    TEST_ASSERT_EQUAL_INT(1, deleted);
}

/** @brief A failed status query preserves resources for the next close attempt. */
static void join_retry(void)
{
    TEST_ASSERT_TRUE(platform_worker_open(&instance, &config, NULL) == 0);

    fail_status_at = 1;

    TEST_ASSERT_EQUAL_INT(PS2_WORKER_ERROR_THREAD_STATUS, platform_worker_close(&instance));
    TEST_ASSERT_EQUAL_INT(0, waits);
    TEST_ASSERT_EQUAL_INT(1, live_threads);
    TEST_ASSERT_EQUAL_INT(2, live_semaphores);
    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
    TEST_ASSERT_EQUAL_INT(0, waits);
}

/** @brief A worker that already exited does not require another completion wait. */
static void already_dormant(void)
{
    TEST_ASSERT_TRUE(platform_worker_open(&instance, &config, NULL) == 0);

    dormant_at = 1;

    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
    TEST_ASSERT_EQUAL_INT(0, waits);
}

/** @brief Failed thread deletion keeps synchronization available until a retry succeeds. */
static void deletion_retry(void)
{
    TEST_ASSERT_TRUE(platform_worker_open(&instance, &config, NULL) == 0);

    dormant_at   = 1;
    delete_error = 1;

    TEST_ASSERT_EQUAL_INT(PS2_WORKER_ERROR_THREAD_DELETE, platform_worker_close(&instance));
    TEST_ASSERT_EQUAL_INT(2, live_semaphores);

    delete_error = 0;

    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
    TEST_ASSERT_EQUAL_INT(1, status_calls);
}

/** @brief Work signals coalesce; close wakes an event-driven loop before joining. */
static void wake_notifications(void)
{
    Ps2WorkerConfig event_config = config;
    event_config.notifications   = 1;

    TEST_ASSERT_TRUE(platform_worker_open(&instance, &event_config, NULL) == 0);
    TEST_ASSERT_EQUAL_INT(3, live_semaphores);
    platform_worker_notify(&instance);
    platform_worker_notify(&instance);
    platform_worker_wait(&instance);
    TEST_ASSERT_EQUAL_INT(0, wake_token);
    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
    TEST_ASSERT_EQUAL_INT(-1, instance.wake);
}

/** @brief Reopening cannot replace handles or reuse a stack before a successful join. */
static void reopen_requires_close(void)
{
    TEST_ASSERT_TRUE(platform_worker_open(&instance, &config, NULL) == 0);

    Ps2Worker      saved   = instance;
    int            created = operation;
    Ps2WorkerError error;

    TEST_ASSERT_TRUE(!(platform_worker_open(&instance, &config, &error) == 0));
    TEST_ASSERT_EQUAL_STRING("WORKER STILL OPEN", error.stage);
    TEST_ASSERT_EQUAL_MEMORY(&saved, &instance, sizeof(instance));
    TEST_ASSERT_EQUAL_INT(created, operation);

    fail_status_at = 1;

    TEST_ASSERT_EQUAL_INT(PS2_WORKER_ERROR_THREAD_STATUS, platform_worker_close(&instance));

    saved = instance;

    TEST_ASSERT_TRUE(!(platform_worker_open(&instance, &config, NULL) == 0));
    TEST_ASSERT_EQUAL_MEMORY(&saved, &instance, sizeof(instance));
    TEST_ASSERT_EQUAL_INT(created, operation);
    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
    TEST_ASSERT_TRUE(platform_worker_open(&instance, &config, NULL) == 0);
    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
}

/** @brief Even an isolated retained semaphore prevents replacement of resources. */
static void partial_resources_reject_open(void)
{
    int* resources[] = { &instance.lock, &instance.done, &instance.wake };

    for (unsigned i = 0; i < 3; ++i)
    {
        *resources[i]   = 7;
        Ps2Worker saved = instance;

        TEST_ASSERT_TRUE(!(platform_worker_open(&instance, &config, NULL) == 0));
        TEST_ASSERT_EQUAL_MEMORY(&saved, &instance, sizeof(instance));
        TEST_ASSERT_EQUAL_INT(0, operation);

        *resources[i] = -1;
    }
}

static void failed_start_retains_thread(void)
{
    fail_at      = 4;
    delete_error = 1;

    TEST_ASSERT_TRUE(!(platform_worker_open(&instance, &config, NULL) == 0));
    TEST_ASSERT_EQUAL_INT(3, instance.thread);
    TEST_ASSERT_EQUAL_INT(1, live_threads);
    TEST_ASSERT_EQUAL_INT(2, live_semaphores);
    TEST_ASSERT_TRUE(!(platform_worker_open(&instance, &config, NULL) == 0));

    delete_error = 0;
    dormant_at   = 1;

    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
    TEST_ASSERT_EQUAL_INT(0, waits);
}

static void semaphore_deletion_retry(void)
{
    Ps2WorkerConfig event_config = config;
    event_config.notifications   = 1;

    TEST_ASSERT_TRUE(platform_worker_open(&instance, &event_config, NULL) == 0);

    sema_delete_error = instance.wake;

    TEST_ASSERT_EQUAL_INT(PS2_WORKER_ERROR_SEMAPHORE_DELETE, platform_worker_close(&instance));
    TEST_ASSERT_EQUAL_INT(-1, instance.thread);
    TEST_ASSERT_EQUAL_INT(-1, instance.lock);
    TEST_ASSERT_EQUAL_INT(-1, instance.done);
    TEST_ASSERT_EQUAL_INT(sema_delete_error, instance.wake);
    TEST_ASSERT_EQUAL_INT(1, live_semaphores);
    TEST_ASSERT_TRUE(!(platform_worker_open(&instance, &event_config, NULL) == 0));

    sema_delete_error = -1;

    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
}

static void rollback_semaphore_deletion_retry(void)
{
    fail_at           = 2;
    sema_delete_error = 1;

    TEST_ASSERT_TRUE(!(platform_worker_open(&instance, &config, NULL) == 0));
    TEST_ASSERT_EQUAL_INT(1, instance.lock);
    TEST_ASSERT_EQUAL_INT(1, live_semaphores);
    TEST_ASSERT_TRUE(!(platform_worker_open(&instance, &config, NULL) == 0));
    TEST_ASSERT_EQUAL_INT(PS2_WORKER_ERROR_SEMAPHORE_DELETE, platform_worker_close(&instance));

    sema_delete_error = -1;

    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
}

/** @brief Failure diagnostics retain the actual SDK operation, resource ID and error code. */
static void cleanup_failure_diagnostics(void)
{
    TEST_ASSERT_TRUE(platform_worker_open(&instance, &config, NULL) == 0);

    fail_status_at = 1;

    TEST_ASSERT_EQUAL_INT(PS2_WORKER_ERROR_THREAD_STATUS, platform_worker_close(&instance));

    fail_status_at = 0;
    delete_error   = 1;

    TEST_ASSERT_EQUAL_INT(PS2_WORKER_ERROR_THREAD_DELETE, platform_worker_close(&instance));

    delete_error      = 0;
    sema_delete_error = instance.lock;

    TEST_ASSERT_EQUAL_INT(PS2_WORKER_ERROR_SEMAPHORE_DELETE, platform_worker_close(&instance));
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_NOT_NULL(strstr(diagnostics, "ReferThreadStatus id=3 failed: -1"));
    TEST_ASSERT_NOT_NULL(strstr(diagnostics, "DeleteThread id=3 failed: -1"));
    TEST_ASSERT_NOT_NULL(strstr(diagnostics, "DeleteSema(lock) id=1 failed: -1"));
#else
    TEST_ASSERT_EQUAL_STRING("", diagnostics);
#endif
    sema_delete_error = -1;

    TEST_ASSERT_EQUAL_INT(0, platform_worker_close(&instance));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(startup_failures);
    RUN_TEST(cleanup_failure_diagnostics);
    RUN_TEST(failed_start_retains_thread);
    RUN_TEST(semaphore_deletion_retry);
    RUN_TEST(rollback_semaphore_deletion_retry);
    RUN_TEST(pending_close_preserves_resources);
    RUN_TEST(join_retry);
    RUN_TEST(already_dormant);
    RUN_TEST(deletion_retry);
    RUN_TEST(wake_notifications);
    RUN_TEST(reopen_requires_close);
    RUN_TEST(partial_resources_reject_open);

    return UNITY_END();
}
