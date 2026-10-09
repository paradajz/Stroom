#include "platform/time/sleep.h"
#include <delaythread.h>
#include <stdint.h>

void platform_sleep_us(uint32_t microseconds)
{
    /* DelayThread takes a signed 32-bit duration. */

    while (microseconds > INT32_MAX)
    {
        DelayThread(INT32_MAX);

        microseconds -= INT32_MAX;
    }

    if (microseconds)
    {
        DelayThread((int32_t)microseconds);
    }
}
