#pragma once

#include <netinet/in.h>

typedef struct
{
    char           netif_name[4];
    struct in_addr ipaddr;
    struct in_addr netmask;
    struct in_addr gw;
    int            dhcp_enabled;
} t_ip_info;

int  ps2ip_init(void);
void ps2ip_deinit(void);
int  libcglue_ps2ip_getconfig(const char* name, t_ip_info* info);
int  libcglue_ps2ip_setconfig(t_ip_info* info);
