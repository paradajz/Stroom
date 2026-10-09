#include "platform/network/runtime.h"
#include "platform/iop/modules.h"
#include "platform/iop/services.h"
#include "platform/network/rpc/bridge.h"
#include "util/diagnostics.h"
#include <ps2ips.h>
#include <stdio.h>
#include <stdint.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

/* Open/close ownership is changed only by the application thread. */
static unsigned      users;
static int           client_pending, closing;
static NetworkConfig startup_config;

extern unsigned char ps2dev9_irx[];
extern unsigned int  size_ps2dev9_irx;
extern unsigned char netman_irx[];
extern unsigned int  size_netman_irx;
extern unsigned char ps2ip_netman_irx[];
extern unsigned int  size_ps2ip_netman_irx;
extern unsigned char smap_netman_irx[];
extern unsigned int  size_smap_netman_irx;
extern unsigned char socket_irx[];
extern unsigned int  size_socket_irx;

/**
 * @brief Wait at most one second for the socket RPC service to register.
 * @return 1 when the service is available; 0 on timeout or binding failure.
 */
static int socket_ready(void)
{
    for (int i = 0; i < PS2_RPC_PROBE_ATTEMPTS; ++i)
    {
        int result = platform_iop_probe(PS2_RPC_SOCKET);

        if (result < 0)
        {
            return 0;
        }

        if (result > 0)
        {
            return 1;
        }

        usleep(PS2_RPC_PROBE_DELAY_US);
    }

    return 0;
}

void platform_network_configure(const NetworkConfig* config)
{
    startup_config = *config;
}

static int close_client(void)
{
    if (!client_pending)
    {
        return 0;
    }

    closing = 1;

    int result = platform_socket_bridge_close();

    if (result < 0)
    {
        STROOM_LOG("socket client close failed (%d)", result);
        return PLATFORM_NETWORK_ERROR_CLEANUP;
    }

    client_pending = closing = 0;

    return 0;
}

int platform_network_startup(char* error, size_t capacity)
{
    if (closing && (users || close_client() != 0))
    {
        snprintf(error, capacity, "SOCKET CLEANUP PENDING");
        return PLATFORM_NETWORK_ERROR_CLEANUP;
    }

    if (users)
    {
        ++users;
        snprintf(error, capacity, "%s", "");
        return 0;
    }

    /* Explicit zero addresses prevent SMAP's built-in static defaults from
     * appearing as a usable address before DHCP has obtained a lease. */
    static const char args[] = "0.0.0.0\0"
                               "0.0.0.0\0"
                               "0.0.0.0";

    snprintf(error, capacity, "%s", "");

    if (platform_iop_module(&(Ps2IopModule){ .name = "dev9", .data = ps2dev9_irx, .size = size_ps2dev9_irx, .args = NULL, .args_size = 0 }, error, capacity) != 0 ||
        platform_iop_module(&(Ps2IopModule){ .name = "Network_Manager", .data = netman_irx, .size = size_netman_irx, .args = NULL, .args_size = 0 }, error, capacity) != 0 ||
        platform_iop_module(&(Ps2IopModule){ .name = "SMAP_driver", .data = smap_netman_irx, .size = size_smap_netman_irx, .args = NULL, .args_size = 0 }, error, capacity) != 0 ||
        platform_iop_module(&(Ps2IopModule){ .name = "TCP/IP Stack", .data = ps2ip_netman_irx, .size = size_ps2ip_netman_irx, .args = args, .args_size = sizeof(args) }, error, capacity) != 0 ||
        platform_iop_module(&(Ps2IopModule){ .name = "STROOM_TCPIP_RPC", .data = socket_irx, .size = size_socket_irx, .args = NULL, .args_size = 0 }, error, capacity) != 0)
    {
        return PLATFORM_NETWORK_ERROR_MODULE_LOAD;
    }

    if (!socket_ready())
    {
        snprintf(error, capacity, "SOCKET RPC UNAVAILABLE");
        return PLATFORM_NETWORK_ERROR_RPC_UNAVAILABLE;
    }

    client_pending = 1;

    int result = ps2ip_init();

    if (result < 0)
    {
        snprintf(error, capacity, "SOCKET INIT %d", result);
        close_client();
        return PLATFORM_NETWORK_ERROR_INITIALIZE;
    }

    if (!platform_socket_bridge_compatible())
    {
        snprintf(error, capacity, "RESTART CONSOLE TO LOAD NETWORK MODULE");
        close_client();
        return PLATFORM_NETWORK_START_RESTART_REQUIRED;
    }

    t_ip_info info = { 0 };

    result = libcglue_ps2ip_getconfig("sm0", &info);

    /* The RPC wrapper can report success even when the interface is absent. */

    if (result < 0 || strcmp(info.netif_name, "sm0") != 0)
    {
        snprintf(error, capacity, "ETHERNET INTERFACE UNAVAILABLE");
        close_client();
        return PLATFORM_NETWORK_ERROR_INTERFACE;
    }

    if (!info.ipaddr.s_addr && !info.dhcp_enabled)
    {
        info.dhcp_enabled = !startup_config.ip;

        if (startup_config.ip)
        {
            info.ipaddr.s_addr  = htonl(startup_config.ip);
            info.netmask.s_addr = htonl(startup_config.netmask);
            info.gw.s_addr      = htonl(startup_config.gateway);
        }

        result = libcglue_ps2ip_setconfig(&info);

        if (result <= 0)
        {
            snprintf(error, capacity, "NETWORK CONFIGURATION %d", result);
            close_client();
            return PLATFORM_NETWORK_ERROR_CONFIGURE;
        }

        /* setconfig can succeed with a stack built without DHCP support. */
        memset(&info, 0, sizeof(info));

        if (libcglue_ps2ip_getconfig("sm0", &info) < 0 || (startup_config.ip ? info.ipaddr.s_addr != htonl(startup_config.ip) : !info.dhcp_enabled))
        {
            snprintf(error, capacity, "NETWORK CONFIGURATION UNAVAILABLE");
            close_client();
            return PLATFORM_NETWORK_ERROR_CONFIG_VERIFY;
        }
    }

    users = 1;

    return 0;
}

void platform_network_address(char* address, size_t capacity)
{
    t_ip_info info = { 0 };

    snprintf(address, capacity, "%s", "");

    if (!users || closing)
    {
        return;
    }

    if (libcglue_ps2ip_getconfig("sm0", &info) >= 0 && info.ipaddr.s_addr)
    {
        uint32_t ip = ntohl(info.ipaddr.s_addr);

        snprintf(address, capacity, "%lu.%lu.%lu.%lu", (unsigned long)(ip >> (3 * 8)), (unsigned long)((ip >> 16) & UINT8_MAX), (unsigned long)((ip >> 8) & UINT8_MAX), (unsigned long)(ip & UINT8_MAX));
    }
}

int platform_network_close(void)
{
    if (users > 1)
    {
        --users;
        return 0;
    }

    if (close_client() != 0)
    {
        return PLATFORM_NETWORK_ERROR_CLEANUP;
    }

    users = 0;

    return 0;
}
