#include "support/worker_driver.h"
#include "platform/thread/worker.h"
#include "unity.h"

static Ps2Worker*      scheduled;
static Ps2WorkerConfig scheduled_config;
static unsigned        opens;

/**
 * @brief Declare the real platform_worker_open implementation used by the linker wrapper.
 */
int __real_platform_worker_open(Ps2Worker* worker, const Ps2WorkerConfig* config, Ps2WorkerError* error);

/**
 * @brief Intercept platform_worker_open calls for fixture assertions and injected outcomes.
 */
int __wrap_platform_worker_open(Ps2Worker* worker, const Ps2WorkerConfig* config, Ps2WorkerError* error)
{
    ++opens;

    scheduled        = worker;
    scheduled_config = *config;

    return __real_platform_worker_open(worker, config, error);
}

/**
 * @brief Control worker reset in the lifecycle fixture.
 */
void test_worker_reset(void)
{
    scheduled        = NULL;
    scheduled_config = (Ps2WorkerConfig){ 0 };
    opens            = 0;
}

/**
 * @brief Control worker run in the lifecycle fixture.
 */
void test_worker_run(void)
{
    TEST_ASSERT_NOT_NULL(scheduled);
    TEST_ASSERT_NOT_NULL(scheduled_config.entry);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, scheduled->thread);

    scheduled->running = 1;

    scheduled_config.entry(scheduled_config.argument);
}

/**
 * @brief Control worker stop in the lifecycle fixture.
 */
void test_worker_stop(void)
{
    if (scheduled)
    {
        scheduled->running = 0;
    }
}

/**
 * @brief Control worker opens in the lifecycle fixture.
 */
unsigned test_worker_opens(void)
{
    return opens;
}

/**
 * @brief Control worker priority in the lifecycle fixture.
 */
int test_worker_priority(void)
{
    return scheduled_config.priority;
}
