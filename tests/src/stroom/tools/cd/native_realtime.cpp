#include "realtime.hpp"
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace stroom::cd
{
    /** @brief Initialize the private PipeWire test's scheduling substitute. */
    Realtime::Realtime() = default;

    /**
     * @brief Check that the private PipeWire test identified a live callback worker.
     * @note This test substitute checks that tid identifies a live worker distinct
     * from the process's main thread. Scheduling is supplied by the test adapter.
     */
    void Realtime::acquire(pid_t tid)
    {
        if (tid == getpid())
        {
            throw SetupError("The network thread must not become the audio callback");
        }

        verify(tid);
    }

    /**
     * @brief Check the test callback's existence through procfs.
     * @note This test substitute checks the task entry for tid in procfs.
     */
    void Realtime::verify(pid_t tid)
    {
        if (tid <= 0 || access(("/proc/self/task/" + std::to_string(tid)).c_str(), F_OK) != 0)
        {
            throw SetupError("Private test callback thread disappeared (tid=" + std::to_string(tid) + ", pid=" + std::to_string(getpid()) + "): " + std::strerror(errno));
        }
    }
}    // namespace stroom::cd
