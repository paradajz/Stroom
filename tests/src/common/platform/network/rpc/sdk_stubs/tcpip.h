#pragma once

#include <tamtypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/ioctl.h>

#ifndef FIONBIO
#define FIONBIO 0x5421
#endif
#define MAXNAMLEN 255

struct fd_set
{
    unsigned long fds_bits[FD_SETSIZE / 8 / sizeof(long)];
};

#define PS2IP_DNS                         1
#define DNS_MAX_SERVERS                   2
#define MEMP_NUM_NETCONN                  9
#define IPADDR_ANY                        0
#define IPADDR4_INIT(value)               { value }
#define ip_addr_copy(destination, source) ((destination) = (source))

typedef struct
{
    u32 addr;
} ip_addr_t;

typedef ip_addr_t      ip4_addr_t;
extern const ip_addr_t ip_addr_any;
#define IP4_ADDR_ANY (&ip_addr_any)

typedef struct
{
    char      netif_name[8];
    ip_addr_t ipaddr, netmask, gw;
    int       dhcp_enabled;
} t_ip_info;

size_t strlcpy(char* destination, const char* source, size_t capacity);
