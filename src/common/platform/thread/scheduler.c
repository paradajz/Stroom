#include "platform/thread/scheduler.h"
#include <kernel.h>

/* EE priorities increase as urgency decreases. audsrv's callback server runs
 * at 0x60 and must preempt the renderer to complete sound initialization. */

static int render_thread = -1;
static int previous_priority;

int platform_scheduler_open(void)
{
    if (render_thread >= 0)
    {
        return 0;
    }

    int                thread = GetThreadId();
    ee_thread_status_t status;

    if (thread < 0 || ReferThreadStatus(thread, &status) < 0)
    {
        return PS2_SCHEDULER_ERROR_THREAD_STATUS;
    }

    if (status.current_priority < PS2_RENDER_PRIORITY && ChangeThreadPriority(thread, PS2_RENDER_PRIORITY) < 0)
    {
        return PS2_SCHEDULER_ERROR_PRIORITY_CHANGE;
    }

    render_thread     = thread;
    previous_priority = status.current_priority;

    return 0;
}

int platform_scheduler_close(void)
{
    if (render_thread < 0)
    {
        return 0;
    }

    if (previous_priority < PS2_RENDER_PRIORITY && ChangeThreadPriority(render_thread, previous_priority) < 0)
    {
        return PS2_SCHEDULER_ERROR_PRIORITY_RESTORE;
    }

    render_thread = -1;

    return 0;
}

int platform_scheduler_background_priority(void)
{
    int                caller = GetThreadId();
    ee_thread_status_t status;

    if (caller < 0 || ReferThreadStatus(caller, &status) < 0 || status.current_priority >= MAX_PRIORITY - 1)
    {
        return PS2_SCHEDULER_ERROR_BACKGROUND_PRIORITY;
    }

    return status.current_priority + 1;
}
