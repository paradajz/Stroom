#include "platform/network/rpc/bridge.h"
#include "platform/network/rpc/driver_stats.h"
#include "util/diagnostics.h"
#include "rpc_fixture.h"
#include "unity.h"
#include <ps2ips.h>
#include <sifrpc.h>
#include <string.h>

static int                 calls, rpc_result, unidentified_bridge, override_reply;
static Ps2SocketBridgeInfo reply;

/**
 * @brief Substitute sceSifCallRpc in this host fixture.
 */
int sceSifCallRpc(SifRpcClientData_t* client, unsigned command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument)
{
    (void)client;

    if (command == PS2_SOCKET_RPC_LINK_READY)
    {
        int32_t zero = 0;

        TEST_ASSERT_EQUAL_INT(sizeof(zero), input_size);
        TEST_ASSERT_EQUAL_INT(sizeof(zero), output_size);
        TEST_ASSERT_EQUAL_MEMORY(&zero, input, sizeof(zero));
        TEST_ASSERT_EQUAL_INT(0, mode);
        TEST_ASSERT_NULL(callback);
        TEST_ASSERT_NULL(argument);
        rpc_fixture_assert_locked();
        ++calls;

        if (!unidentified_bridge && rpc_result >= 0)
        {
            memcpy(output, rpc_peer_call(command, input, input_size), sizeof(zero));
        }

        return rpc_result;
    }

    if (command == PS2_SOCKET_RPC_TX_STATS)
    {
        Ps2NetTxStats zero = { 0 };

        TEST_ASSERT_EQUAL_INT(sizeof(zero), input_size);
        TEST_ASSERT_EQUAL_INT(sizeof(zero), output_size);
        TEST_ASSERT_EQUAL_MEMORY(&zero, input, sizeof(zero));
        TEST_ASSERT_EQUAL_INT(0, mode);
        TEST_ASSERT_NULL(callback);
        TEST_ASSERT_NULL(argument);
        rpc_fixture_assert_locked();
        ++calls;

        if (!unidentified_bridge)
        {
            memcpy(output, rpc_peer_call(command, input, input_size), sizeof(zero));
        }

        return rpc_result;
    }

    Ps2SocketBridgeInfo zero = { 0 };

    TEST_ASSERT_EQUAL_INT(PS2_SOCKET_RPC_INFO, command);
    TEST_ASSERT_EQUAL_INT(0, mode);
    TEST_ASSERT_EQUAL_INT(sizeof(zero), input_size);
    TEST_ASSERT_EQUAL_INT(sizeof(zero), output_size);
    TEST_ASSERT_EQUAL_MEMORY(&zero, input, sizeof(zero));
    TEST_ASSERT_NULL(callback);
    TEST_ASSERT_NULL(argument);
    rpc_fixture_assert_locked();
    ++calls;

    if (!unidentified_bridge)
    {
        if (!override_reply)
        {
            memcpy(&reply, rpc_peer_call(command, input, input_size), sizeof(reply));
        }

        memcpy(output, &reply, sizeof(reply));
    }

    return rpc_result;
}

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    override_reply      = 0;
    unidentified_bridge = 0;
    rpc_result          = 0;
    calls               = 0;

    memset(&reply, 0, sizeof(reply));
    rpc_peer_open();
    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());
}

/**
 * @brief Finish fixture cleanup and resource checks after each test.
 */
void tearDown(void)
{
    ps2ip_deinit();
}

/**
 * @brief Verify capabilities.
 */
static void capabilities(void)
{
    TEST_ASSERT_TRUE(platform_socket_bridge_compatible());
    TEST_ASSERT_EQUAL_HEX32(PS2_SOCKET_BRIDGE_MAGIC, reply.magic);
    TEST_ASSERT_EQUAL_UINT(PS2_SOCKET_BRIDGE_ABI, reply.abi);
    TEST_ASSERT_EQUAL_UINT(PS2_SOCKET_CAP_LINK_READY | (STROOM_DIAGNOSTICS ? PS2_SOCKET_CAP_DIAGNOSTICS : 0), reply.capabilities);

    override_reply     = 1;
    reply.capabilities = PS2_SOCKET_CAP_LINK_READY | PS2_SOCKET_CAP_DIAGNOSTICS;

    TEST_ASSERT_TRUE(platform_socket_bridge_compatible());

    reply.capabilities = PS2_SOCKET_CAP_LINK_READY;

    TEST_ASSERT_EQUAL_INT(!STROOM_DIAGNOSTICS, platform_socket_bridge_compatible());

    reply.capabilities = PS2_SOCKET_CAP_DIAGNOSTICS;

    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());
}

/**
 * @brief Verify incompatible.
 */
static void incompatible(void)
{
    TEST_ASSERT_TRUE(platform_socket_bridge_compatible());

    override_reply = 1;

    ++reply.abi;
    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());

    reply.abi   = PS2_SOCKET_BRIDGE_ABI;
    reply.magic = 0;

    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());

    unidentified_bridge = 1;

    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());
}

/**
 * @brief Verify unavailable.
 */
static void unavailable(void)
{
    rpc_result = -1;

    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());
    TEST_ASSERT_EQUAL_INT(1, calls);
    ps2ip_deinit();
    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());
    TEST_ASSERT_EQUAL_INT(1, calls);
}

/**
 * @brief Verify transmit status uses compatible readonly rpc.
 */
static void transmit_status_uses_compatible_readonly_rpc(void)
{
    Ps2NetTxStats stats;

    platform_net_tx_stats(&stats);
    TEST_ASSERT_EQUAL_INT(1, stats.status);
    TEST_ASSERT_EQUAL_UINT(37, stats.tx_underruns);

    unidentified_bridge = 1;

    platform_net_tx_stats(&stats);
    TEST_ASSERT_EQUAL_INT(0, stats.status);
    TEST_ASSERT_EQUAL_UINT(0, stats.tx_underruns);

    unidentified_bridge = 0;
    rpc_result          = -1;

    platform_net_tx_stats(&stats);
    TEST_ASSERT_EQUAL_INT(0, stats.status);
    ps2ip_deinit();
    platform_net_tx_stats(&stats);
    TEST_ASSERT_EQUAL_INT(0, stats.status);
    TEST_ASSERT_EQUAL_INT(3, calls);
}

/**
 * @brief Verify link readiness is readonly and checked.
 */
static void link_readiness_is_readonly_and_checked(void)
{
    TEST_ASSERT_TRUE(platform_socket_link_ready());
    rpc_peer_link(0);
    TEST_ASSERT_FALSE(platform_socket_link_ready());
    rpc_peer_link(1);
    TEST_ASSERT_TRUE(platform_socket_link_ready());
    rpc_peer_link(-1);
    TEST_ASSERT_FALSE(platform_socket_link_ready());

    unidentified_bridge = 1;

    TEST_ASSERT_FALSE(platform_socket_link_ready());

    unidentified_bridge = 0;
    rpc_result          = -1;

    TEST_ASSERT_FALSE(platform_socket_link_ready());
    ps2ip_deinit();
    TEST_ASSERT_FALSE(platform_socket_link_ready());
    TEST_ASSERT_EQUAL_INT(6, calls);
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(capabilities);
    RUN_TEST(incompatible);
    RUN_TEST(unavailable);
    RUN_TEST(transmit_status_uses_compatible_readonly_rpc);
    RUN_TEST(link_readiness_is_readonly_and_checked);

    return UNITY_END();
}
