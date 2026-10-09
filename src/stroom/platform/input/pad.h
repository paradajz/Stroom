#pragma once

#include "platform/input/input.h"

/** Failure codes for this API. */
typedef enum
{
    PS2_PAD_ERROR_MODULE_LOAD     = -1,
    PS2_PAD_ERROR_RPC_UNAVAILABLE = -2,
    PS2_PAD_ERROR_INITIALIZE      = -3,
    PS2_PAD_ERROR_PORT_OPEN       = -4,
    PS2_PAD_ERROR_PORT_CLOSE      = -5,
} Ps2PadError;

/**
 * @brief Initialize controller modules and open port zero.
 *
 * A successful open resets button press history.
 *
 * @return 0 on success, a negative Ps2PadError on failure.
 */
int platform_pad_open(void);

/**
 * @brief Read controller state and derive portable button press edges.
 *
 * @param enabled Nonzero if platform_pad_open succeeded.
 * @return Held and newly pressed buttons plus connection status.
 */
InputState platform_pad_read(int enabled);

/**
 * @brief Close controller port zero if it was opened.
 *
 * @param enabled Nonzero if the controller was initialized.
 * @return 0 on success, a negative Ps2PadError on failure.
 * before reopening when closing fails.
 */
int platform_pad_close(int enabled);
