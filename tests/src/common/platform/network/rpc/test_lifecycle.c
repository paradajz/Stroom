#include "platform/network/rpc/bridge.h"
#include "rpc_fixture.h"
#include "unity.h"
#include <ps2ips.h>
#include <sifrpc.h>
#include <errno.h>
#include <sys/socket.h>

int sceSifCallRpc(SifRpcClientData_t* client, unsigned command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument)
{
    (void)client;
    (void)command;
    (void)mode;
    (void)input;
    (void)input_size;
    (void)output;
    (void)output_size;
    (void)callback;
    (void)argument;
    TEST_FAIL_MESSAGE("Disconnected client must not issue RPC calls");

    return -1;
}

void setUp(void)
{
    rpc_fixture_lifecycle(7, 0, 0);
}

void tearDown(void)
{
    rpc_fixture_lifecycle(7, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, platform_socket_bridge_close());
}

static void creation_failure_does_not_connect(void)
{
    rpc_fixture_lifecycle(-42, 0, 0);
    TEST_ASSERT_EQUAL_INT(-42, ps2ip_init());
    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());
    TEST_ASSERT_EQUAL_INT(-1, socket(AF_INET, SOCK_DGRAM, 0));
    TEST_ASSERT_EQUAL_INT(ENOSYS, errno);
    TEST_ASSERT_EQUAL_INT(0, platform_socket_bridge_close());
    TEST_ASSERT_EQUAL_INT(0, rpc_fixture_deletions());
    rpc_fixture_lifecycle(7, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());
    TEST_ASSERT_EQUAL_INT(1, rpc_fixture_creations());
}

static void deletion_failure_retains_handle_and_blocks_reopen(void)
{
    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());
    rpc_fixture_lifecycle(7, -42, 0);
    TEST_ASSERT_EQUAL_INT(-42, platform_socket_bridge_close());
    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());
    TEST_ASSERT_EQUAL_INT(-1, socket(AF_INET, SOCK_DGRAM, 0));
    TEST_ASSERT_EQUAL_INT(ENOSYS, errno);
    TEST_ASSERT_EQUAL_INT(-EBUSY, ps2ip_init());
    TEST_ASSERT_EQUAL_INT(0, rpc_fixture_creations());
    TEST_ASSERT_EQUAL_INT(-42, platform_socket_bridge_close());
    TEST_ASSERT_EQUAL_INT(2, rpc_fixture_deletions());
    rpc_fixture_lifecycle(7, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, platform_socket_bridge_close());
    TEST_ASSERT_EQUAL_INT(1, rpc_fixture_deletions());
    TEST_ASSERT_EQUAL_INT(0, platform_socket_bridge_close());
    TEST_ASSERT_EQUAL_INT(1, rpc_fixture_deletions());
    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());
    TEST_ASSERT_EQUAL_INT(1, rpc_fixture_creations());
}

static void reboot_cleanup_failure_is_reported(void)
{
    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());
    rpc_fixture_lifecycle(7, -42, 1);
    TEST_ASSERT_EQUAL_INT(-42, ps2ip_init());
    TEST_ASSERT_EQUAL_INT(0, rpc_fixture_creations());
    TEST_ASSERT_EQUAL_INT(-EBUSY, ps2ip_init());
    TEST_ASSERT_FALSE(platform_socket_bridge_compatible());
}

static void legacy_close_preserves_failed_cleanup(void)
{
    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());
    rpc_fixture_lifecycle(7, -42, 0);
    ps2ip_deinit();
    TEST_ASSERT_EQUAL_INT(-EBUSY, ps2ip_init());
    TEST_ASSERT_EQUAL_INT(-42, platform_socket_bridge_close());
    TEST_ASSERT_EQUAL_INT(2, rpc_fixture_deletions());
}

static void zero_semaphore_handle_is_released(void)
{
    rpc_fixture_lifecycle(0, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());
    TEST_ASSERT_EQUAL_INT(0, platform_socket_bridge_close());
    TEST_ASSERT_EQUAL_INT(1, rpc_fixture_deletions());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(creation_failure_does_not_connect);
    RUN_TEST(zero_semaphore_handle_is_released);
    RUN_TEST(deletion_failure_retains_handle_and_blocks_reopen);
    RUN_TEST(reboot_cleanup_failure_is_reported);
    RUN_TEST(legacy_close_preserves_failed_cleanup);

    return UNITY_END();
}
