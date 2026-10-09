#pragma once

#include <stdint.h>

/**
 * @brief Advance the xorshift32 sequence using its fixed shift triplet.
 * @param state Current PRNG state; seed selection belongs to the caller.
 * @return Next PRNG state; a zero seed remains zero.
 */
static inline uint32_t util_random_next_u32(uint32_t state)
{
    enum
    {
        SHIFT_1 = 13,
        SHIFT_2 = 17,
        SHIFT_3 = 5
    };

    state ^= state << SHIFT_1;
    state ^= state >> SHIFT_2;
    state ^= state << SHIFT_3;

    return state;
}
