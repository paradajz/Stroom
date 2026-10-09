#include <ps2ips.h>
#include <ps2ip_rpc.h>
#include <sifrpc.h>
#include <sys/socket.h>
#include "rpc_fixture.h"
#include "unity.h"
#include <string.h>

static int transport_result;
static int fragment_response;
static int datagram;

int sceSifCallRpc(SifRpcClientData_t* client, unsigned command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument)
{
    (void)client;
    TEST_ASSERT_EQUAL_INT(0, mode);
    rpc_fixture_assert_locked();

    if (transport_result < 0)
    {
        return transport_result;
    }

    if (fragment_response && (command == PS2IPS_ID_RECV || command == PS2IPS_ID_RECVFROM))
    {
        const s_recv_pkt request    = *(s_recv_pkt*)input;
        rests_pkt        completion = { .ssize = 64, .esize = 64, .sbuf = request.ee_addr, .ebuf = (u8*)request.ee_addr + 64 };

        memset(completion.sbuffer, 'A', sizeof(completion.sbuffer));
        memset(completion.ebuffer, 'B', sizeof(completion.ebuffer));
        memcpy(request.intr_data, &completion, sizeof(completion));
        *(r_recv_pkt*)output = (r_recv_pkt){ .ret = 128 };
    }
    else
    {
        memcpy(output, rpc_peer_call(command, input, input_size), (size_t)output_size);
    }

    if (callback)
    {
        callback(argument);
    }

    return 0;
}

static ssize_t receive(void* data, size_t capacity)
{
    if (datagram)
    {
        struct sockaddr peer;
        socklen_t       size = sizeof(peer);

        return recvfrom(17, data, capacity, 0, &peer, &size);
    }

    return recv(17, data, capacity, 0);
}

void setUp(void)
{
    transport_result = fragment_response = datagram = 0;

    rpc_peer_open();
    TEST_ASSERT_EQUAL_INT(0, ps2ip_init());
    TEST_ASSERT_EQUAL_INT(17, socket(AF_INET, SOCK_DGRAM, 0));
}

void tearDown(void)
{
    ps2ip_deinit();
}

static void short_receives_preserve_guards(void)
{
    const unsigned lengths[] = { 1, 7, 31, 63, 64 };
    unsigned char  memory[192];

    for (datagram = 0; datagram < 2; ++datagram)
    {
        for (unsigned offset = 0; offset < 64; ++offset)
        {
            for (unsigned i = 0; i < sizeof(lengths) / sizeof(*lengths); ++i)
            {
                memset(memory, 'X', sizeof(memory));
                rpc_peer_receive((int)lengths[i]);
                TEST_ASSERT_EQUAL_INT(lengths[i], receive(memory + 64 + offset, 64));
                TEST_ASSERT_EACH_EQUAL_UINT8('X', memory, 64 + offset);
                TEST_ASSERT_EACH_EQUAL_UINT8('A', memory + 64 + offset, lengths[i]);
                TEST_ASSERT_EACH_EQUAL_UINT8('X', memory + 64 + offset + lengths[i], sizeof(memory) - 64 - offset - lengths[i]);
            }
        }
    }
}

static void empty_reads_do_not_reuse_fragment_destinations(void)
{
    unsigned char old[128], next[128];

    for (datagram = 0; datagram < 2; ++datagram)
    {
        fragment_response = 1;

        TEST_ASSERT_EQUAL_INT(128, receive(old, sizeof(old)));
        TEST_ASSERT_EACH_EQUAL_UINT8('A', old, 64);
        TEST_ASSERT_EACH_EQUAL_UINT8('B', old + 64, 64);

        fragment_response = 0;

        memset(old, 'X', sizeof(old));
        memset(next, 'Y', sizeof(next));

        for (int result = 0; result >= -1; --result)
        {
            rpc_peer_receive(result);
            TEST_ASSERT_EQUAL_INT(result, receive(next, sizeof(next)));
            TEST_ASSERT_EACH_EQUAL_UINT8('X', old, sizeof(old));
            TEST_ASSERT_EACH_EQUAL_UINT8('Y', next, sizeof(next));
        }

        rpc_peer_receive(7);
        TEST_ASSERT_EQUAL_INT(7, receive(next, sizeof(next)));
        TEST_ASSERT_EACH_EQUAL_UINT8('A', next, 7);
        TEST_ASSERT_EACH_EQUAL_UINT8('Y', next + 7, sizeof(next) - 7);
    }
}

static void empty_first_read_and_transport_failure_preserve_buffer(void)
{
    unsigned char data[64];

    memset(data, 'X', sizeof(data));
    TEST_ASSERT_EQUAL_INT(0, receive(data, sizeof(data)));

    transport_result = -1;

    TEST_ASSERT_EQUAL_INT(-1, receive(data, sizeof(data)));
    TEST_ASSERT_EACH_EQUAL_UINT8('X', data, sizeof(data));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(short_receives_preserve_guards);
    RUN_TEST(empty_reads_do_not_reuse_fragment_destinations);
    RUN_TEST(empty_first_read_and_transport_failure_preserve_buffer);

    return UNITY_END();
}
