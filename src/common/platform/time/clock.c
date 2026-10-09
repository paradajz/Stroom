#include "platform/time/clock.h"
#include "util/time_units.h"
#include <timer.h>

#define MICROSECONDS_PER_SECOND 1000000u

uint64_t platform_ticks(void)
{
    return GetTimerSystemTime();
}

uint64_t platform_ticks_to_us(uint64_t ticks)
{
    /* Split before multiplying to avoid overflowing long timestamps. */
    return ticks / kBUSCLK * MICROSECONDS_PER_SECOND + ticks % kBUSCLK * MICROSECONDS_PER_SECOND / kBUSCLK;
}

double platform_ticks_to_us_fractional(uint64_t ticks)
{
    return (double)ticks * MICROSECONDS_PER_SECOND / kBUSCLK;
}

uint32_t platform_millis(void)
{
    return (uint32_t)(platform_ticks() / (kBUSCLK / MILLISECONDS_PER_SECOND));
}
