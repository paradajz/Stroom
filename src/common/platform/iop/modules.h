#pragma once

#include <stddef.h>

/** Failure codes for this API. */
typedef enum
{
    PS2_IOP_ERROR_MODULE_LOAD  = -1,
    PS2_IOP_ERROR_MODULE_START = -2,
    PS2_IOP_ERROR_RPC_BIND     = -3,
} Ps2IopError;

/**
 * @brief Resident module identity and its fallback load source.
 */
typedef struct
{
    const char* name;      /**< Resident module name. */
    const char* path;      /**< ROM path, or NULL for an embedded image. */
    void*       data;      /**< Embedded bytes; unused with a path. */
    unsigned    size;      /**< Embedded image size in bytes. */
    const char* args;      /**< Null-separated arguments, or NULL. */
    unsigned    args_size; /**< Argument byte count, including terminators. */
} Ps2IopModule;

/**
 * @brief Reuse a resident module or load its ROM/embedded fallback.
 * @param module Module identity, image, and arguments.
 * @param error Optional diagnostic destination; NULL when capacity is zero.
 * @param capacity Diagnostic destination size in bytes.
 * @return 0 on success, a negative Ps2IopError on failure.
 */
int platform_iop_module(const Ps2IopModule* module, char* error, size_t capacity);

/**
 * @brief Probe one RPC service without retaining shared client state.
 * @param id RPC service identifier.
 * @return 1 when available, 0 when absent, PS2_IOP_ERROR_RPC_BIND on binding failure.
 */
int platform_iop_probe(unsigned id);
