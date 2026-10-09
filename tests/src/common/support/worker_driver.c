#include "support/worker_driver.h"
#include "platform/thread/worker.h"
#include "unity.h"

static Ps2Worker*      scheduled;
static Ps2WorkerConfig scheduled_config;
static unsigned        opens;

int __real_platform_worker_open(Ps2Worker* worker, const Ps2WorkerConfig* config, Ps2WorkerError* error);

int __wrap_platform_worker_open(Ps2Worker* worker, const Ps2WorkerConfig* config, Ps2WorkerError* error)
{
    ++opens;

    scheduled        = worker;
    scheduled_config = *config;

    return __real_platform_worker_open(worker, config, error);
}

void test_worker_reset(void)
{
    scheduled        = NULL;
    scheduled_config = (Ps2WorkerConfig){ 0 };
    opens            = 0;
}

void test_worker_run(void)
{
    TEST_ASSERT_NOT_NULL(scheduled);
    TEST_ASSERT_NOT_NULL(scheduled_config.entry);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, scheduled->thread);

    scheduled->running = 1;

    scheduled_config.entry(scheduled_config.argument);
}

void test_worker_stop(void)
{
    if (scheduled)
    {
        scheduled->running = 0;
    }
}

unsigned test_worker_opens(void)
{
    return opens;
}

int test_worker_priority(void)
{
    return scheduled_config.priority;
}
