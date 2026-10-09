#include "platform/network/rpc/bridge.h"
#include "util/diagnostics.h"
#include "rpc_fixture.h"
#include "unity.h"
#include <ps2ips.h>
#include <sifrpc.h>
#include <string.h>

static int                 calls, rpc_result, legacy, override_reply;
static Ps2SocketBridgeInfo reply;

int sceSifCallRpc(SifRpcClientData_t* client, unsigned command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument)
{
    (void)client;

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

    if (!legacy)
    {
        if (!override_reply)
        {
            memcpy(&reply, rpc_peer_call(command, input, input_size), sizeof(reply));
        }

        memcpy(output, &reply, sizeof(reply));
    }

    return rpc_result;
}

void setUp(void)
{
    calls = rpc_result = legacy = override_reply = 0;

    memset(&reply, 0, sizeof(reply));
    rpc_peer_open();
    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());
}

void tearDown(void)
{
    ps2ip_deinit();
}

static void capabilities(void)
{
    TEST_ASSERT_TRUE(platform_socket_bridge_compatible());
    TEST_ASSERT_EQUAL_HEX32(PS2_SOCKET_BRIDGE_MAGIC, reply.magic);
    TEST_ASSERT_EQUAL_UINT(PS2_SOCKET_BRIDGE_ABI, reply.abi);
    TEST_ASSERT_EQUAL_UINT(STROOM_DIAGNOSTICS ? PS2_SOCKET_CAP_DIAGNOSTICS : 0, reply.capabilities);

    override_reply     = 1;
    reply.capabilities = PS2_SOCKET_CAP_DIAGNOSTICS;

    TEST_ASSERT_TRUE(platform_socket_bridge_compatible());

    reply.capabilities = 0;

    TEST_ASSERT_EQUAL_INT(!STROOM_DIAGNOSTICS, platform_socket_bridge_compatible());
}

static void incompatible(void)
{
    TEST_ASSERT_TRUE(platform_socket_bridge_compatible());

    override_reply = 1;

    ++reply.abi;
    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());

    reply.abi   = PS2_SOCKET_BRIDGE_ABI;
    reply.magic = 0;

    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());

    legacy = 1;

    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());
}

static void unavailable(void)
{
    rpc_result = -1;

    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());
    TEST_ASSERT_EQUAL_INT(1, calls);
    ps2ip_deinit();
    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());
    TEST_ASSERT_EQUAL_INT(1, calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(capabilities);
    RUN_TEST(incompatible);
    RUN_TEST(unavailable);

    return UNITY_END();
}
