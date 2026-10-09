#pragma once

#include <stdint.h>

#define PS2_SOCKET_RPC_INFO        130
#define PS2_SOCKET_BRIDGE_MAGIC    0x50534252u
#define PS2_SOCKET_BRIDGE_ABI      1u
#define PS2_SOCKET_CAP_DIAGNOSTICS 1u

/**
 * @brief Socket bridge identity shared by EE and IOP; independent of stack tracing.
 */
typedef struct
{
    uint32_t magic;        /**< PS2_SOCKET_BRIDGE_MAGIC, absent in legacy bridge replies. */
    uint32_t abi;          /**< Socket bridge ABI; increment when existing commands become incompatible. */
    uint32_t capabilities; /**< Optional PS2_SOCKET_CAP_* commands implemented by this bridge. */
} Ps2SocketBridgeInfo;

/**
 * @brief Query the bound bridge before using sockets or diagnostic commands.
 * @return Nonzero for the current ABI with all features required by this build;
 * zero for a legacy/incompatible bridge or RPC failure.
 */
int platform_socket_bridge_compatible(void);

/**
 * @brief Disconnect the EE client and release its semaphore after callers stop.
 * Failed deletion retains the handle and blocks initialization until a close
 * retry succeeds. The SDK's void ps2ip_deinit() delegates to this operation.
 * @return 0 on success; negative SDK error on failure.
 */
int platform_socket_bridge_close(void);
