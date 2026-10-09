#pragma once
#define NO_SYS                  0
#define LWIP_TCPIP_CORE_LOCKING 1
#define LWIP_NETCONN            1

#ifndef LWIP_SOCKET
#define LWIP_SOCKET 0
#endif
#define LWIP_UDP         0
#define LWIP_RAW         0
#define LWIP_DNS         0
#define LWIP_IGMP        0
#define LWIP_IPV6        0
#define LWIP_STATS       0
#define LWIP_SO_SNDTIMEO 0
#define LWIP_SO_LINGER   0
#define TCP_MSS          1460
#define TCP_SND_BUF      (TCP_MSS * 4)
