#include "platform/platform.h"
#include "util/diagnostics.h"
#include "platform/thread/scheduler.h"
#include "platform/network/runtime.h"
#include <debug.h>
#include <sifrpc.h>
#include <stdio.h>

int platform_open(void)
{
    sceSifInitRpc(0);
    init_scr();
    scr_setXY(2, 4);
    setvbuf(stdout, NULL, _IONBF, 0);

    if (platform_scheduler_open() != 0)
    {
        scr_printf("stroom: renderer priority setup failed\n");
        return PLATFORM_ERROR_SCHEDULER_START;
    }

    return 0;
}

int platform_close(void)
{
    /* Device owners have closed; retry any failed network startup rollback. */

    int result = platform_network_close();

    if (result != 0)
    {
        return PLATFORM_ERROR_NETWORK_CLOSE;
    }

    int restored = platform_scheduler_close();

    if (restored != 0)
    {
        STROOM_LOG("renderer priority restoration failed");
    }

    return restored < 0 ? PLATFORM_ERROR_SCHEDULER_CLOSE : restored;
}
