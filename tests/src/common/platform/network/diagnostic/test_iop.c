#include "stroom_diagnostic_hooks.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "intrman.h"
#include "unity.h"
#include <string.h>

static uint32_t           now;
static int                interrupt_depth;
static Ps2DiagnosticReply reply;

/**
 * @brief Supply a deterministic IOP clock.
 * @return Test milliseconds.
 */
uint32_t sys_now(void)
{
    return now;
}

/**
 * @brief Model nested interrupt-state preservation.
 * @param state Previous nesting depth.
 * @return Zero.
 */
int CpuSuspendIntr(int* state)
{
    *state = interrupt_depth++;

    return 0;
}

/**
 * @brief Restore the saved interrupt state.
 * @param state Saved depth.
 * @return Zero.
 */
int CpuResumeIntr(int state)
{
    interrupt_depth = state;

    return 0;
}

/**
 * @brief Return the selected fixture's peer or an invalid-socket failure.
 * @param fd Fixture socket, 3.
 * @param address Peer destination.
 * @param size Destination size.
 * @return Zero on success, minus one otherwise.
 */
int lwip_getpeername(int fd, struct sockaddr* address, socklen_t* size)
{
    if (fd != 3 || *size < sizeof(struct sockaddr_in))
    {
        return -1;
    }

    struct sockaddr_in peer = { .sin_family = AF_INET, .sin_port = htons(40000) };

    peer.sin_addr.s_addr = htonl(0xc0a80121);

    memcpy(address, &peer, sizeof(peer));

    return 0;
}

/**
 * @brief Select the fixture connection and clear pending observations.
 */
void setUp(void)
{
    now             = 100;
    interrupt_depth = 0;

    Ps2DiagnosticRequest request = { .abi = DIAGNOSTIC_IOP_ABI, .command = DIAGNOSTIC_IOP_SELECT, .socket = 3 };

    platform_net_diagnostic_service(&request, &reply);
    TEST_ASSERT_EQUAL_INT(1, reply.status);
}

/**
 * @brief Verify all interrupt-protected operations restore their state.
 */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT(0, interrupt_depth);
}

/**
 * @brief Record only the selected connection, preserving TCP fields and byte progress.
 */
static void tcp_and_reads(void)
{
    struct tcp_pcb pcb = { .local_port = 12889, .remote_port = 40001, .remote_ip = htonl(0xc0a80121), .rcv_nxt = 123, .rcv_wnd = 32768 };

    platform_net_diagnostic_rx(&pcb, 100, 200, 1460, 1000, 16);

    pcb.remote_port = 40000;

    platform_net_diagnostic_rx(&pcb, 100, 200, 1460, 1000, 16);
    platform_net_diagnostic_delivery(&pcb, 1460, 0);
    platform_net_diagnostic_delivery(&pcb, 120, -1);

    struct tcp_hdr header = { .seqno = htonl(300), .ackno = htonl(400), .wnd = htons(0), .offset_flags = htons(0x5010) };

    platform_net_diagnostic_tx(&pcb, &header, sizeof(header), 0);

    Ps2DiagnosticRequest request = { .abi = DIAGNOSTIC_IOP_ABI, .command = DIAGNOSTIC_IOP_READ_END, .socket = 3, .result = 512 };

    platform_net_diagnostic_service(&request, NULL);

    request.command = DIAGNOSTIC_IOP_POLL;

    platform_net_diagnostic_service(&request, &reply);
    TEST_ASSERT_EQUAL_UINT(6, reply.count);
    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_IOP_SACK_STATE, reply.records[0].kind);
    TEST_ASSERT_EQUAL_UINT(0, reply.records[0].data[1]);
    TEST_ASSERT_EQUAL_UINT(123, reply.records[1].data[4]);
    TEST_ASSERT_EQUAL_UINT(1460, reply.records[3].data[2]);
    TEST_ASSERT_EQUAL_UINT(400, reply.records[4].data[1]);
    TEST_ASSERT_EQUAL_UINT(0, reply.records[4].data[2]);
    TEST_ASSERT_EQUAL_UINT(512, reply.records[5].data[1]);
    platform_net_diagnostic_service(&request, &reply);
    TEST_ASSERT_EQUAL_UINT(0, reply.count);
}

/**
 * @brief Bound bursts and explicitly count overwritten IOP observations.
 */
static void overflow_and_disable(void)
{
    for (unsigned i = 0; i < DIAGNOSTIC_IOP_RING + 9; ++i)
    {
        platform_net_diagnostic_failure(DIAGNOSTIC_IOP_POOL_ERROR, (int)i);
    }

    Ps2DiagnosticRequest request = { .abi = DIAGNOSTIC_IOP_ABI, .command = DIAGNOSTIC_IOP_POLL };

    platform_net_diagnostic_service(&request, &reply);
    TEST_ASSERT_EQUAL_UINT(9, reply.lost);
    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_IOP_BATCH, reply.count);
    TEST_ASSERT_EQUAL_UINT(9, reply.records[0].data[0]);

    request.command = DIAGNOSTIC_IOP_SELECT;
    request.socket  = -1;

    platform_net_diagnostic_service(&request, &reply);
    platform_net_diagnostic_failure(DIAGNOSTIC_IOP_INPUT_ERROR, -1);

    request.command = DIAGNOSTIC_IOP_POLL;

    platform_net_diagnostic_service(&request, &reply);
    TEST_ASSERT_EQUAL_UINT(0, reply.count);
    TEST_ASSERT_EQUAL_UINT(0, reply.lost);
}

/**
 * @brief Retain negotiation state periodically without adding a record per packet.
 */
static void sack_negotiation(void)
{
    struct tcp_pcb       pcb  = { .local_port = 12889, .remote_port = 40000, .remote_ip = htonl(0xc0a80121), .flags = TF_SACK };
    Ps2DiagnosticRequest poll = { .abi = DIAGNOSTIC_IOP_ABI, .command = DIAGNOSTIC_IOP_POLL };

    platform_net_diagnostic_rx(&pcb, 0, 0, 0, 0, 16);
    platform_net_diagnostic_service(&poll, &reply);
    TEST_ASSERT_EQUAL_UINT(2, reply.count);
    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_IOP_SACK_STATE, reply.records[0].kind);
    TEST_ASSERT_EQUAL_UINT(1, reply.records[0].data[0]);
    TEST_ASSERT_EQUAL_UINT(1, reply.records[0].data[1]);
    platform_net_diagnostic_rx(&pcb, 0, 0, 0, 0, 16);
    platform_net_diagnostic_service(&poll, &reply);
    TEST_ASSERT_EQUAL_UINT(1, reply.count);

    now += 1000;

    platform_net_diagnostic_rx(&pcb, 0, 0, 0, 0, 16);
    platform_net_diagnostic_service(&poll, &reply);
    TEST_ASSERT_EQUAL_UINT(2, reply.count);

    pcb.flags = 0;

    platform_net_diagnostic_rx(&pcb, 0, 0, 0, 0, 16);
    platform_net_diagnostic_service(&poll, &reply);
    TEST_ASSERT_EQUAL_UINT(2, reply.count);
    TEST_ASSERT_EQUAL_UINT(0, reply.records[0].data[1]);
}

/**
 * @brief Preserve four actual wire ranges, sequence wrap and failed IP submission.
 */
static void sack_wire_ranges(void)
{
    struct tcp_pcb pcb = { .local_port = 12889, .remote_port = 40000, .remote_ip = htonl(0xc0a80121), .flags = TF_SACK };

    struct
    {
        struct tcp_hdr header;      /**< Actual fixed TCP header layout. */
        unsigned char  options[40]; /**< Maximum TCP option bytes. */
    } packet = { .header = { .ackno = htonl(123), .offset_flags = htons(0xe010) }, .options = { 1, 1, 5, 34 } };

    const uint32_t edges[] = { 0xfffffff0u, 16, 100, 200, 300, 400, 500, 600 };

    for (unsigned i = 0; i < 8; ++i)
    {
        uint32_t network = htonl(edges[i]);

        memcpy(packet.options + 4 + i * sizeof(network), &network, sizeof(network));
    }

    platform_net_diagnostic_tx(&pcb, &packet.header, 56, -1);

    Ps2DiagnosticRequest poll = { .abi = DIAGNOSTIC_IOP_ABI, .command = DIAGNOSTIC_IOP_POLL };

    platform_net_diagnostic_service(&poll, &reply);
    TEST_ASSERT_EQUAL_UINT(7, reply.count);
    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_IOP_SACK_PACKET, reply.records[2].kind);
    TEST_ASSERT_EQUAL_UINT(4, reply.records[2].data[2]);
    TEST_ASSERT_EQUAL_UINT(1, reply.records[2].data[3]);
    TEST_ASSERT_EQUAL_UINT(56, reply.records[2].data[4]);

    for (unsigned i = 0; i < 4; ++i)
    {
        const Ps2DiagnosticRecord* block = &reply.records[3 + i];

        TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_IOP_SACK_BLOCK, block->kind);
        TEST_ASSERT_EQUAL_UINT(reply.records[2].data[0], block->data[0]);
        TEST_ASSERT_EQUAL_UINT(123, block->data[1]);
        TEST_ASSERT_EQUAL_UINT(i, block->data[2]);
        TEST_ASSERT_EQUAL_UINT(edges[i * 2], block->data[3]);
        TEST_ASSERT_EQUAL_UINT(edges[i * 2 + 1], block->data[4]);
        TEST_ASSERT_EQUAL_HEX32(0xffffffffu, block->data[5]);
    }

    /* The declared options cannot exceed the caller's available bytes. */
    platform_net_diagnostic_tx(&pcb, &packet.header, 24, 0);
    platform_net_diagnostic_service(&poll, &reply);
    TEST_ASSERT_EQUAL_UINT(2, reply.count);
    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_IOP_SACK_PACKET, reply.records[1].kind);
    TEST_ASSERT_EQUAL_UINT(0, reply.records[1].data[3]);
    TEST_ASSERT_EQUAL_UINT(0, reply.records[1].data[2]);

    /* Reject malformed option lengths without emitting partial ranges. */
    packet.options[3] = 3;

    platform_net_diagnostic_tx(&pcb, &packet.header, 56, 0);
    platform_net_diagnostic_service(&poll, &reply);
    TEST_ASSERT_EQUAL_UINT(2, reply.count);
    TEST_ASSERT_EQUAL_UINT(0, reply.records[1].data[3]);

    /* End-of-options ignores trailing padding, even if it resembles SACK. */
    packet.options[0] = 0;

    platform_net_diagnostic_tx(&pcb, &packet.header, 56, 0);
    platform_net_diagnostic_service(&poll, &reply);
    TEST_ASSERT_EQUAL_UINT(1, reply.count);

    pcb.remote_port   = 40001;
    packet.options[0] = 1;
    packet.options[3] = 34;

    platform_net_diagnostic_tx(&pcb, &packet.header, 56, 0);
    platform_net_diagnostic_service(&poll, &reply);
    TEST_ASSERT_EQUAL_UINT(0, reply.count);
}

/**
 * @brief Run host tests of the actual IOP recorder implementation.
 * @return Unity failure count.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(tcp_and_reads);
    RUN_TEST(sack_negotiation);
    RUN_TEST(sack_wire_ranges);
    RUN_TEST(overflow_and_disable);

    return UNITY_END();
}
