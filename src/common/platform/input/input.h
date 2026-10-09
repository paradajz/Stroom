#pragma once

/* Platform-neutral buttons shared by application and player controllers. */
enum
{
    INPUT_UP       = 1u << 0,
    INPUT_DOWN     = 1u << 1,
    INPUT_LEFT     = 1u << 2,
    INPUT_RIGHT    = 1u << 3,
    INPUT_CROSS    = 1u << 4,
    INPUT_SQUARE   = 1u << 6,
    INPUT_TRIANGLE = 1u << 7,
    INPUT_L2       = 1u << 5,
    INPUT_L1       = 1u << 8,
    INPUT_R1       = 1u << 9,
    INPUT_R2       = 1u << 10,
    INPUT_START    = 1u << 11,
    INPUT_SELECT   = 1u << 12
};

/**
 * @brief Platform-neutral controller state for one frame.
 */
typedef struct
{
    unsigned held;      /**< Currently held INPUT_* button bits. */
    unsigned pressed;   /**< Newly pressed INPUT_* button edges. */
    int      connected; /**< Nonzero when the controller can be read. */
} InputState;
