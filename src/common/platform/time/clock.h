#pragma once

#include <stdint.h>

/**
 * @brief Read the PS2 monotonic clock in bus-clock ticks.
 * @return Raw 64-bit timer value, without unit conversion.
 */
uint64_t platform_ticks(void);

/**
 * @brief Convert timer ticks to whole microseconds, rounding down.
 * @param ticks Platform timer ticks.
 * @return Elapsed whole microseconds.
 */
uint64_t platform_ticks_to_us(uint64_t ticks);

/**
 * @brief Convert timer ticks to microseconds while retaining fractions.
 * @param ticks Platform timer ticks.
 * @return Elapsed microseconds for profiling.
 */
double platform_ticks_to_us_fractional(uint64_t ticks);

/**
 * @brief Read the PS2 monotonic clock.
 *
 * @return Milliseconds as a wrapping 32-bit counter.
 */
uint32_t platform_millis(void);
