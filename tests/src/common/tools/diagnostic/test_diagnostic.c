#include "unity.h"
#include "support/process.h"
#include <unistd.h>

/**
 * @brief No fixture state is shared across tests.
 */
void setUp(void)
{
}

/**
 * @brief The process helper releases child resources.
 */
void tearDown(void)
{
}

/**
 * @brief Exercise the real downloader against dropped, stale and unavailable UDP pages.
 */
static void transfer_fixture(void)
{
    execl(NODE_PATH, NODE_PATH, FIXTURE_PATH, CLIENT_PATH, (char*)NULL);
    TEST_FAIL_MESSAGE("Could not launch diagnostic downloader fixture");
}

/**
 * @brief Bound fixture execution and clean up all descendants.
 */
static void transfer(void)
{
    test_process_run(transfer_fixture);
}

/**
 * @brief Run diagnostic download integration tests.
 * @return Unity failure count.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(transfer);

    return UNITY_END();
}
