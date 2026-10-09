#include "platform/network/diagnostic/wire.h"
#include "rpc_fixture.h"
#include "unity.h"
#include <ps2ips.h>
#include <ps2ip_rpc.h>
#include <sifrpc.h>
#include <sys/socket.h>
#include <string.h>

static int                  calls, rpc_result;
static Ps2DiagnosticRequest captured;

int sceSifCallRpc(SifRpcClientData_t* client, unsigned command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument)
{
    (void)client;
    rpc_fixture_assert_locked();

    if (command == PS2IPS_ID_SOCKET)
    {
        *(s32*)output = 4;
        return 0;
    }

    TEST_ASSERT_EQUAL_INT(DIAGNOSTIC_IOP_RPC, command);
    TEST_ASSERT_EQUAL_INT(0, mode);
    TEST_ASSERT_EQUAL_INT(sizeof(Ps2DiagnosticRequest), input_size);
    TEST_ASSERT_EQUAL_INT(sizeof(Ps2DiagnosticReply), output_size);
    TEST_ASSERT_NULL(callback);
    TEST_ASSERT_NULL(argument);
    memcpy(&captured, input, sizeof(captured));

    Ps2DiagnosticReply reply = { .status = 1, .abi = DIAGNOSTIC_IOP_ABI };

    memcpy(output, &reply, sizeof(reply));
    ++calls;

    return rpc_result;
}

void setUp(void)
{
    calls = rpc_result = 0;

    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());
    TEST_ASSERT_EQUAL_INT(17, socket(AF_INET, SOCK_DGRAM, 0));
}

void tearDown(void)
{
    ps2ip_deinit();
}

/**
 * @brief Select the IOP socket without mutating the caller's public descriptor.
 */
static void translates_selection(void)
{
    Ps2DiagnosticRequest request = { .abi = DIAGNOSTIC_IOP_ABI, .command = DIAGNOSTIC_IOP_SELECT, .socket = 17 };
    Ps2DiagnosticReply   reply;

    platform_net_diagnostic(&request, &reply);
    TEST_ASSERT_EQUAL_INT(4, captured.socket);
    TEST_ASSERT_EQUAL_INT(17, request.socket);
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_INT(1, reply.status);
}

/**
 * @brief Reject unmapped descriptors before sending an invalid selection.
 */
static void rejects_invalid_descriptor(void)
{
    Ps2DiagnosticRequest request = { .abi = DIAGNOSTIC_IOP_ABI, .command = DIAGNOSTIC_IOP_SELECT, .socket = 17 };
    Ps2DiagnosticReply   reply;

    request.socket = 18;

    platform_net_diagnostic(&request, &reply);
    TEST_ASSERT_EQUAL_INT(-3, reply.status);
    TEST_ASSERT_EQUAL_INT(0, calls);
}

/**
 * @brief Preserve the disable sentinel and polling requests without translation.
 */
static void preserves_nonselection_requests(void)
{
    Ps2DiagnosticRequest request = { .abi = DIAGNOSTIC_IOP_ABI, .command = DIAGNOSTIC_IOP_SELECT, .socket = -1 };
    Ps2DiagnosticReply   reply;

    platform_net_diagnostic(&request, &reply);
    TEST_ASSERT_EQUAL_INT(-1, captured.socket);

    request.command = DIAGNOSTIC_IOP_POLL;
    request.socket  = 17;

    platform_net_diagnostic(&request, &reply);
    TEST_ASSERT_EQUAL_INT(17, captured.socket);
}

/**
 * @brief Report missing or failed RPC transport without claiming support.
 */
static void reports_transport_failure(void)
{
    Ps2DiagnosticRequest request = { .abi = DIAGNOSTIC_IOP_ABI, .command = DIAGNOSTIC_IOP_POLL };
    Ps2DiagnosticReply   reply;

    ps2ip_deinit();
    platform_net_diagnostic(&request, &reply);
    TEST_ASSERT_EQUAL_INT(-2, reply.status);
    TEST_ASSERT_EQUAL_INT(0, calls);
    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());

    rpc_result = -1;

    platform_net_diagnostic(&request, &reply);
    TEST_ASSERT_EQUAL_INT(-2, reply.status);
}

/**
 * @brief Run the actual patched EE diagnostic wrapper with host RPC substitutes.
 * @return Failed test count.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(translates_selection);
    RUN_TEST(rejects_invalid_descriptor);
    RUN_TEST(preserves_nonselection_requests);
    RUN_TEST(reports_transport_failure);

    return UNITY_END();
}
