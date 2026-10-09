#pragma once

#include <stdint.h>

/* xorshift32's fixed shift triplet defines the sequence. Seed policy is caller-owned. */
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
