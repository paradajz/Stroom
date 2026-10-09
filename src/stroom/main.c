#include "app/runtime.h"
#include "platform/time/sleep.h"
#include "platform/time/clock.h"
#include <stdio.h>

#define CLEANUP_RETRY_US  1000
#define CLEANUP_REPORT_MS 5000

int main(int argc, char** argv)
{
    static AppRuntime runtime;

    if (app_runtime_open(&runtime, argc, argv) != 0)
    {
        uint32_t started  = platform_millis();
        int      reported = 0;
        int      result;

        while ((result = app_runtime_close(&runtime)) != 0)
        {
            if (!reported && (uint32_t)(platform_millis() - started) >= CLEANUP_REPORT_MS)
            {
                fprintf(stderr, "stroom: cleanup taking too long (result %d) - restart console; retaining resources and continuing cleanup\n", result);

                reported = 1;
            }

            platform_sleep_us(CLEANUP_RETRY_US);
        }

        return 1;
    }

    for (;;)
    {
        app_runtime_step(&runtime);
    }
}
