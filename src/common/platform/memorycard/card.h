#pragma once

/** Failure codes for memory-card settings access. */
typedef enum
{
    PS2_MEMORYCARD_ERROR_MODULE_LOAD     = -1,
    PS2_MEMORYCARD_ERROR_RPC_UNAVAILABLE = -2,
    PS2_MEMORYCARD_ERROR_INITIALIZE      = -3,
    PS2_MEMORYCARD_ERROR_IO              = -4,
    PS2_MEMORYCARD_ERROR_UNAVAILABLE     = -5,
} Ps2MemorycardError;

/**
 * @brief Initialize resident or ROM card services for a memory-card executable.
 * Does not reset the IOP, write the card or require MagicGate signing support.
 * @param executable Executable path used to identify its boot device.
 * @return Zero on success or a non-card boot; a negative Ps2MemorycardError when card settings cannot be read.
 */
int platform_memorycard_open(const char* executable);
