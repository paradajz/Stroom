#pragma once

#include <stdint.h>

/**
 * @brief Suspend the calling EE thread for a duration in microseconds.
 * @param microseconds Requested delay; zero returns immediately.
 * Other runnable threads can execute during the delay. Scheduling can extend it.
 */
void platform_sleep_us(uint32_t microseconds);
