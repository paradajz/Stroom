#include "platform/thread/scheduler.h"
#include <kernel.h>
#include "unity.h"

static int thread_id        = 7;
static int current_priority = 2;
static int fail_status;
static int fail_change;
static int changes;

int GetThreadId(void)
{
    return thread_id;
}

int ReferThreadStatus(int thread, ee_thread_status_t* status)
{
    TEST_ASSERT_TRUE_MESSAGE(thread == 7, "thread == 7");

    status->current_priority = current_priority;

    return fail_status ? -1 : 0;
}

int ChangeThreadPriority(int thread, int priority)
{
    TEST_ASSERT_TRUE_MESSAGE(thread == 7, "thread == 7");
    ++changes;

    if (fail_change)
    {
        return -1;
    }

    current_priority = priority;

    return 0;
}

/**
 * @brief Verify scheduling ownership, failure cleanup, and original-priority restoration.
 *
 */
static void scheduler_regressions(void)
{
    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_close() == 0 && changes == 0, "platform_scheduler_close() && changes == 0");

    thread_id = -1;

    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_open() != 0, "!platform_scheduler_open()");

    thread_id   = 7;
    fail_status = 1;

    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_open() != 0 && changes == 0, "!platform_scheduler_open() && changes == 0");
    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_close() == 0 && changes == 0, "platform_scheduler_close() && changes == 0");

    fail_status = 0;
    fail_change = 1;

    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_open() != 0 && changes == 1 && current_priority == 2, "!platform_scheduler_open() && changes == 1 && current_priority == 2");
    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_close() == 0 && changes == 1, "platform_scheduler_close() && changes == 1");

    fail_change = 0;

    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_open() == 0 && current_priority == 0x61 && changes == 2, "platform_scheduler_open() && current_priority == 0x61 && changes == 2");
    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_open() == 0 && changes == 2, "platform_scheduler_open() && changes == 2");

    fail_change = 1;

    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_close() != 0 && current_priority == 0x61 && changes == 3, "!platform_scheduler_close() && current_priority == 0x61 && changes == 3");

    fail_change = 0;

    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_close() == 0 && current_priority == 2 && changes == 4, "platform_scheduler_close() && current_priority == 2 && changes == 4");
    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_close() == 0 && changes == 4, "platform_scheduler_close() && changes == 4");
    /* Already-safe priorities are neither raised nor needlessly rewritten. */

    for (int original = 0x61; original <= 0x70; ++original)
    {
        current_priority = original;

        TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_open() == 0 && current_priority == original && changes == 4, "platform_scheduler_open() && current_priority == original && changes == 4");
        TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_close() == 0 && current_priority == original && changes == 4, "platform_scheduler_close() && current_priority == original && changes == 4");
    }

    current_priority = 0x60;

    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_open() == 0 && current_priority == 0x61, "platform_scheduler_open() && current_priority == 0x61");
    TEST_ASSERT_TRUE_MESSAGE(platform_scheduler_close() == 0 && current_priority == 0x60, "platform_scheduler_close() && current_priority == 0x60");
}

/**
 * @brief Fixtures are initialized by each scenario.
 */
void setUp(void)
{
    thread_id        = 7;
    current_priority = 2;
    fail_status = fail_change = changes = 0;
}

/**
 * @brief This suite owns no external resources.
 */
void tearDown(void)
{
    fail_change = 0;

    TEST_ASSERT_TRUE(platform_scheduler_close() == 0);
}

/**
 * @brief Run the regression scenario.
 * @return Number of failed Unity cases.
 */
/** @brief Choose background priority without changing caller scheduling. */
static void background_priority(void)
{
    TEST_ASSERT_EQUAL_INT(3, platform_scheduler_background_priority());
    TEST_ASSERT_EQUAL_INT(0, changes);

    current_priority = MAX_PRIORITY - 2;

    TEST_ASSERT_EQUAL_INT(MAX_PRIORITY - 1, platform_scheduler_background_priority());

    current_priority = MAX_PRIORITY - 1;

    TEST_ASSERT_EQUAL_INT(PS2_SCHEDULER_ERROR_BACKGROUND_PRIORITY, platform_scheduler_background_priority());

    current_priority = 0x61;
    fail_status      = 1;

    TEST_ASSERT_EQUAL_INT(PS2_SCHEDULER_ERROR_BACKGROUND_PRIORITY, platform_scheduler_background_priority());

    fail_status = 0;
    thread_id   = -1;

    TEST_ASSERT_EQUAL_INT(PS2_SCHEDULER_ERROR_BACKGROUND_PRIORITY, platform_scheduler_background_priority());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(scheduler_regressions);
    RUN_TEST(background_priority);

    return UNITY_END();
}
