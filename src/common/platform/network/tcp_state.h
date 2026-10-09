#pragma once

#include "platform/network/tcp_state_wire.h"

/** Return one for a compatible read-only snapshot; zero for an older stack or failed query. */
int platform_tcp_state(int fd, Ps2TcpState* state);
