#pragma once

#include "platform/network/tcp_state_wire.h"

/**
 * @brief Query a validated read-only TCP diagnostic snapshot.
 * @param fd TCP socket descriptor.
 * @param state Destination cleared when the query fails or its ABI is unsupported.
 * @return One for a valid snapshot, zero for an unsupported or failed query.
 */
int platform_tcp_state(int fd, Ps2TcpState* state);
