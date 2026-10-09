#pragma once

#include <stdint.h>

#define kBUSCLK 147456000

/**
 * @brief Substitute GetTimerSystemTime in this host fixture.
 */
uint64_t GetTimerSystemTime(void);
