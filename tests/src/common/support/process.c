#include "process.h"
#include "unity.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

void test_process_run(void (*scenario)(void))
{
    /* Host tests run in the Linux development container. Adopt orphaned
     * child processes so failure cleanup joins them, rather than only signaling them. */
    int previous_subreaper = 0;

    TEST_ASSERT_EQUAL_INT(0, prctl(PR_GET_CHILD_SUBREAPER, &previous_subreaper));
    TEST_ASSERT_EQUAL_INT(0, prctl(PR_SET_CHILD_SUBREAPER, 1));
    fflush(NULL);

    pid_t child = fork();

    if (child < 0)
    {
        prctl(PR_SET_CHILD_SUBREAPER, previous_subreaper);
        TEST_FAIL_MESSAGE("Cannot fork isolated case");
    }

    if (!child)
    {
        if (setpgid(0, 0) < 0)
        {
            _exit(125);
        }

        if (TEST_PROTECT())
        {
            scenario();
        }

        if (Unity.CurrentTestFailed)
        {
            putchar('\n');
        }

        fflush(NULL);
        _exit(Unity.CurrentTestFailed ? 1 : 0);
    }

    /* Establish the group from both sides so timeout cleanup cannot race startup. */
    setpgid(child, child);

    int finished = 0;

    for (unsigned i = 0; i < 1000; ++i)
    {
        siginfo_t info   = { 0 };
        int       result = waitid(P_PID, (id_t)child, &info, WEXITED | WNOHANG | WNOWAIT);

        if (result == 0 && info.si_pid == child)
        {
            finished = 1;

            break;
        }

        if (result < 0 && errno != EINTR)
        {
            break;
        }

        struct timespec delay = { 0, 10000000 };

        nanosleep(&delay, NULL);
    }

    /* Keep the leader unreaped until group cleanup to avoid PID reuse. */
    kill(-child, SIGKILL);
    kill(child, SIGKILL);

    int   status = 0;
    pid_t result;

    do
    {
        result = waitpid(child, &status, 0);
    } while (result < 0 && errno == EINTR);

    /* The group leader has exited, so any surviving descendants are now ours. */
    pid_t descendant;

    do
    {
        descendant = waitpid(-child, NULL, 0);
    } while (descendant > 0 || (descendant < 0 && errno == EINTR));

    int restored = prctl(PR_SET_CHILD_SUBREAPER, previous_subreaper);

    TEST_ASSERT_EQUAL_INT(0, restored);
    TEST_ASSERT_TRUE_MESSAGE(finished, "Isolated case timed out or could not be observed");
    TEST_ASSERT_TRUE_MESSAGE(result == child && WIFEXITED(status), "Isolated case crashed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, WEXITSTATUS(status), "Isolated case failed; see child diagnostics");
}
