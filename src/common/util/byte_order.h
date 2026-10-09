#pragma once
#include <stdint.h>

/** Byte shifts in a 32-bit network-order word. */
enum
{
    UTIL_BE32_HIGH_SHIFT   = 24,
    UTIL_BE32_MIDDLE_SHIFT = 16,
    UTIL_BE32_LOW_SHIFT    = 8
};

/**
 * @brief Read an unaligned 32-bit big-endian word without platform dependencies.
 * @param bytes At least four readable bytes.
 * @return Host-order value.
 */
static inline uint32_t util_read_be32(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << UTIL_BE32_HIGH_SHIFT) | ((uint32_t)bytes[1] << UTIL_BE32_MIDDLE_SHIFT) | ((uint32_t)bytes[2] << UTIL_BE32_LOW_SHIFT) | bytes[3];
}

/**
 * @brief Write an unaligned 32-bit big-endian word without platform dependencies.
 * @param bytes At least four writable bytes.
 * @param value Host-order value.
 */
static inline void util_write_be32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> UTIL_BE32_HIGH_SHIFT);
    bytes[1] = (uint8_t)(value >> UTIL_BE32_MIDDLE_SHIFT);
    bytes[2] = (uint8_t)(value >> UTIL_BE32_LOW_SHIFT);
    bytes[3] = (uint8_t)value;
}
