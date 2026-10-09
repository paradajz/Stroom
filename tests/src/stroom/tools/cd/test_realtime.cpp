#include "realtime.hpp"
#include <sched.h>
#include <cassert>
#include <iostream>

using stroom::cd::Realtime;
using stroom::cd::SetupError;

static int  observed_policy   = SCHED_RR | SCHED_RESET_ON_FORK;
static int  observed_priority = 5;
static bool failed_query      = false;

/**
 * @brief Substitute the configured kernel policy for scheduling checks.
 * @return Configured policy or a simulated query failure.
 * @note The thread-ID argument is unused by this test substitute.
 */
extern "C" int __wrap_sched_getscheduler(pid_t)
{
    return observed_policy;
}

/**
 * @brief Substitute kernel priority retrieval and an optional query failure.
 * @param[out] result Receives the configured priority, including on simulated failure.
 * @return Zero on success, or minus one when failed_query is set.
 * @note The thread-ID argument is unused by this test substitute.
 */
extern "C" int __wrap_sched_getparam(pid_t, sched_param* result)
{
    result->sched_priority = observed_priority;

    return failed_query ? -1 : 0;
}

/**
 * @brief Assert that production verification rejects the configured grant.
 * @param tid Thread ID passed to the production policy verifier.
 */
static void rejected(pid_t tid)
{
    bool failed = false;

    try
    {
        Realtime::verify(tid);
    }
    catch (const SetupError&)
    {
        failed = true;
    }

    assert(failed);
}

/**
 * @brief Exercise production policy verification through scheduling-query substitutes.
 * @return Zero after all grant and revocation assertions pass.
 */
int main()
{
    Realtime::verify(123);
    rejected(0);

    for (int policy : { SCHED_OTHER, SCHED_RR, SCHED_FIFO | SCHED_RESET_ON_FORK, -1 })
    {
        observed_policy = policy;
        rejected(123);
    }

    observed_policy   = SCHED_RR | SCHED_RESET_ON_FORK;
    observed_priority = 0;

    rejected(123);

    observed_priority = 20;

    rejected(123);

    observed_priority = 5;
    failed_query      = true;

    rejected(123);

    failed_query = false;

    Realtime::verify(123);

    observed_policy = SCHED_OTHER;

    rejected(123);
    std::cout << "Production RT grant verification rejects ordinary, wrong, missing and revoked policies\n";
}
