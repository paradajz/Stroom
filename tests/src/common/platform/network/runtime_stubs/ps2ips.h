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

/**
 * @brief Substitute ps2ip_init in this host fixture.
 */
int ps2ip_init(void);

/**
 * @brief Substitute ps2ip_deinit in this host fixture.
 */
void ps2ip_deinit(void);

/**
 * @brief Substitute libcglue_ps2ip_getconfig in this host fixture.
 */
int libcglue_ps2ip_getconfig(const char* name, t_ip_info* info);

/**
 * @brief Substitute libcglue_ps2ip_setconfig in this host fixture.
 */
int libcglue_ps2ip_setconfig(t_ip_info* info);
