#pragma once

#include <sys/socket.h>

/* The ps2ips bridge forwards option levels unchanged. PS2SDK's IOP tcpip.h
 * uses 0x0fff, unlike the EE sys/socket.h value 0xffff. */
#ifdef _EE
#define PLATFORM_SOCKET_LEVEL 0x0fff
#else
#define PLATFORM_SOCKET_LEVEL SOL_SOCKET
#endif
