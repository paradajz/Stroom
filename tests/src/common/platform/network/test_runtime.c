#include "platform/network/runtime.h"
#include "platform/iop/modules.h"
#include "platform/network/rpc/bridge.h"
#include <ps2ips.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"

unsigned char    ps2dev9_irx[1], netman_irx[1], ps2ip_netman_irx[1], smap_netman_irx[1], socket_irx[1];
unsigned int     size_ps2dev9_irx, size_netman_irx, size_ps2ip_netman_irx, size_smap_netman_irx, size_socket_irx;
static unsigned  initialized, deinitialized;
static int       compatible, close_result, init_result, link_ready;
static t_ip_info current;
static unsigned  configurations;
static unsigned  module_count;

/**
 * @brief Substitute platform_iop_module in this host fixture.
 */
int platform_iop_module(const Ps2IopModule* module, char* error, size_t capacity)
{
    const char* names[] = { "dev9", "Network_Manager", "SMAP_driver", "TCP/IP Stack", "STROOM_TCPIP_RPC" };
    unsigned    index   = module_count++ % 5;

    TEST_ASSERT_EQUAL_STRING(names[index], module->name);

    if (index == 3)
    {
        const char* address = module->args;

        TEST_ASSERT_NOT_NULL(address);

        for (unsigned i = 0; i < 3; ++i)
        {
            TEST_ASSERT_EQUAL_STRING("0.0.0.0", address);

            address += strlen(address) + 1;
        }

        TEST_ASSERT_EQUAL_UINT(module->args_size, (unsigned)(address - module->args));
    }
    else
    {
        TEST_ASSERT_EQUAL_INT(0, module->args_size);
    }

    (void)error;
    (void)capacity;

    return 0;
}

/**
 * @brief Substitute platform_iop_probe in this host fixture.
 */
int platform_iop_probe(unsigned id)
{
    (void)id;

    return 1;
}

/**
 * @brief Substitute ps2ip_init in this host fixture.
 */
int ps2ip_init(void)
{
    ++initialized;

    return init_result;
}

/**
 * @brief Substitute platform_socket_bridge_close in this host fixture.
 */
int platform_socket_bridge_close(void)
{
    ++deinitialized;

    return close_result;
}

/**
 * @brief Substitute platform_socket_bridge_compatible in this host fixture.
 */
int platform_socket_bridge_compatible(void)
{
    return compatible;
}

/**
 * @brief Substitute platform_socket_link_ready in this host fixture.
 */
int platform_socket_link_ready(void)
{
    return link_ready;
}

/**
 * @brief Substitute libcglue_ps2ip_getconfig in this host fixture.
 */
int libcglue_ps2ip_getconfig(const char* name, t_ip_info* info)
{
    snprintf(info->netif_name, sizeof(info->netif_name), "%s", name);

    *info = current;

    snprintf(info->netif_name, sizeof(info->netif_name), "%s", name);

    return 0;
}

/**
 * @brief Substitute libcglue_ps2ip_setconfig in this host fixture.
 */
int libcglue_ps2ip_setconfig(t_ip_info* info)
{
    current = *info;

    ++configurations;

    return 1;
}

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    deinitialized  = 0;
    initialized    = 0;
    compatible     = 1;
    link_ready     = 1;
    init_result    = 0;
    close_result   = 0;
    current        = (t_ip_info){ .ipaddr.s_addr = 1 };
    module_count   = 0;
    configurations = 0;

    platform_network_configure(&(NetworkConfig){ 0 });
}

/**
 * @brief Finish fixture cleanup and resource checks after each test.
 */
void tearDown(void)
{
    close_result = 0;

    TEST_ASSERT_TRUE(platform_network_close() == 0);
}

/**
 * @brief Verify shared socket ownership.
 */
static void shared_socket_ownership(void)
{
    char error[128];

    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_EQUAL_UINT(1, initialized);
    TEST_ASSERT_EQUAL_UINT(5, module_count);
    platform_network_close();
    TEST_ASSERT_EQUAL_UINT(0, deinitialized);
    platform_network_close();
    TEST_ASSERT_EQUAL_UINT(1, deinitialized);
    platform_network_close();
    TEST_ASSERT_EQUAL_UINT(1, deinitialized);
}

/**
 * @brief Verify failed probe does not claim network.
 */
static void failed_probe_does_not_claim_network(void)
{
    char error[128];

    compatible = 0;

    TEST_ASSERT_EQUAL_INT(PLATFORM_NETWORK_START_RESTART_REQUIRED, platform_network_startup(error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("RESTART CONSOLE TO LOAD NETWORK MODULE", error);
    platform_network_close();
    TEST_ASSERT_EQUAL_UINT(1, deinitialized);

    compatible = 1;

    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_EQUAL_UINT(2, initialized);
    platform_network_close();
    TEST_ASSERT_EQUAL_UINT(2, deinitialized);
}

/**
 * @brief Verify static configuration preserves launcher.
 */
static void static_configuration_preserves_launcher(void)
{
    char error[128];

    platform_network_configure(&(NetworkConfig){ .ip = 0xc0a801f0, .netmask = 0xffffff00, .gateway = 0xc0a80101 });
    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_EQUAL_UINT(0, configurations);
    platform_network_close();

    current.ipaddr.s_addr = 0;
    current.dhcp_enabled  = 1;

    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_EQUAL_UINT(0, configurations);
    platform_network_close();

    current.dhcp_enabled = 0;

    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_EQUAL_UINT(1, configurations);
    TEST_ASSERT_EQUAL_HEX32(0xc0a801f0, ntohl(current.ipaddr.s_addr));
    TEST_ASSERT_EQUAL_HEX32(0xffffff00, ntohl(current.netmask.s_addr));
    TEST_ASSERT_EQUAL_HEX32(0xc0a80101, ntohl(current.gw.s_addr));
    TEST_ASSERT_FALSE(current.dhcp_enabled);
}

/**
 * @brief Verify default configuration uses dhcp.
 */
static void default_configuration_uses_dhcp(void)
{
    char error[128];

    current.ipaddr.s_addr = 0;

    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_TRUE(current.dhcp_enabled);
}

/**
 * @brief Verify failed close retains last owner.
 */
static void failed_close_retains_last_owner(void)
{
    char error[128], address[32];

    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_TRUE(platform_network_close() == 0);

    close_result = -42;

    TEST_ASSERT_TRUE(!(platform_network_close() == 0));
    TEST_ASSERT_TRUE(!(platform_network_startup(error, sizeof(error)) == 0));
    TEST_ASSERT_EQUAL_STRING("SOCKET CLEANUP PENDING", error);
    TEST_ASSERT_EQUAL_UINT(1, initialized);
    platform_network_address(address, sizeof(address));
    TEST_ASSERT_EQUAL_STRING("", address);
    TEST_ASSERT_TRUE(!(platform_network_close() == 0));

    close_result = 0;

    TEST_ASSERT_TRUE(platform_network_close() == 0);
    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_EQUAL_UINT(2, initialized);
}

/**
 * @brief Verify failed rollback blocks startup until cleaned.
 */
static void failed_rollback_blocks_startup_until_cleaned(void)
{
    char error[128];

    compatible   = 0;
    close_result = -42;

    TEST_ASSERT_EQUAL_INT(PLATFORM_NETWORK_START_RESTART_REQUIRED, platform_network_startup(error, sizeof(error)));
    TEST_ASSERT_EQUAL_INT(PLATFORM_NETWORK_ERROR_CLEANUP, platform_network_startup(error, sizeof(error)));
    TEST_ASSERT_EQUAL_UINT(1, initialized);
    TEST_ASSERT_TRUE(!(platform_network_close() == 0));

    close_result = 0;
    compatible   = 1;

    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
    TEST_ASSERT_EQUAL_UINT(2, initialized);
}

/**
 * @brief Verify failed initialization rolls back.
 */
static void failed_initialization_rolls_back(void)
{
    char error[128];

    init_result = -42;

    TEST_ASSERT_TRUE(!(platform_network_startup(error, sizeof(error)) == 0));
    TEST_ASSERT_EQUAL_STRING("SOCKET INIT -42", error);
    TEST_ASSERT_EQUAL_UINT(1, deinitialized);
    TEST_ASSERT_TRUE(platform_network_close() == 0);
    TEST_ASSERT_EQUAL_UINT(1, deinitialized);

    init_result = 0;

    TEST_ASSERT_TRUE(platform_network_startup(error, sizeof(error)) == 0);
}

/**
 * @brief Verify ready address requires link and ip.
 */
static void ready_address_requires_link_and_ip(void)
{
    char error[128], address[16];

    platform_network_ready_address(address, sizeof(address));
    TEST_ASSERT_EQUAL_STRING("", address);
    TEST_ASSERT_EQUAL_INT(0, platform_network_startup(error, sizeof(error)));

    link_ready = 0;

    platform_network_ready_address(address, sizeof(address));
    TEST_ASSERT_EQUAL_STRING("", address);
    platform_network_address(address, sizeof(address));
    TEST_ASSERT_EQUAL_STRING("1.0.0.0", address);

    link_ready            = 1;
    current.ipaddr.s_addr = 0;

    platform_network_ready_address(address, sizeof(address));
    TEST_ASSERT_EQUAL_STRING("", address);

    current.ipaddr.s_addr = 1;

    platform_network_ready_address(address, sizeof(address));
    TEST_ASSERT_EQUAL_STRING("1.0.0.0", address);
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(static_configuration_preserves_launcher);
    RUN_TEST(default_configuration_uses_dhcp);
    RUN_TEST(shared_socket_ownership);
    RUN_TEST(failed_close_retains_last_owner);
    RUN_TEST(failed_rollback_blocks_startup_until_cleaned);
    RUN_TEST(failed_initialization_rolls_back);
    RUN_TEST(failed_probe_does_not_claim_network);
    RUN_TEST(ready_address_requires_link_and_ip);

    return UNITY_END();
}
