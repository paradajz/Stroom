#pragma once

#include <stdint.h>

/**
 * @brief Read the PS2 monotonic clock in bus-clock ticks.
 * @return Raw 64-bit timer value, without unit conversion.
 */
uint64_t platform_ticks(void);

/** @brief Convert timer ticks to whole microseconds, rounding down. */
uint64_t platform_ticks_to_us(uint64_t ticks);

/** @brief Convert timer ticks to microseconds, retaining fractions for profiling. */
double platform_ticks_to_us_fractional(uint64_t ticks);

/**
 * @brief Read the PS2 monotonic clock.
 *
 * @return Milliseconds as a wrapping 32-bit counter.
 */
uint32_t platform_millis(void);
