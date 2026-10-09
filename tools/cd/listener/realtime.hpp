#pragma once

#include "listener.hpp"
#include <sys/types.h>

namespace stroom::cd
{
    /** @brief Bound RT CPU time, request priority 5 through RealtimeKit, and verify the kernel grant. All calls belong on an ordinary-priority thread. */
    class Realtime
    {
        public:
        /**
         * @brief Read RealtimeKit limits and bound the process's RT CPU time.
         * @throws SetupError On unavailable limits, an unsuitable grant policy or
         * failure to read or set RLIMIT_RTTIME.
         * @note Construct before PipeWire workers start; tighter existing limits are retained.
         */
        Realtime();

        /**
         * @brief Request and verify priority 5 for this process's audio callback thread.
         * @param tid Kernel thread ID published by the callback.
         * @throws SetupError On a failed RealtimeKit request or an invalid kernel grant.
         * @note Requests SCHED_RR with SCHED_RESET_ON_FORK from an ordinary-priority thread.
         */
        void acquire(pid_t tid);

        /**
         * @brief Check that the exact granted policy and priority remain present.
         * @param tid Kernel thread ID of the granted audio callback.
         * @throws SetupError On a nonpositive or vanished thread ID, a failed kernel
         * query, missing SCHED_RR/reset-on-fork policy, or priority other than 5.
         */
        static void verify(pid_t tid);

        private:
        static constexpr unsigned priority = 5;
    };
}    // namespace stroom::cd
