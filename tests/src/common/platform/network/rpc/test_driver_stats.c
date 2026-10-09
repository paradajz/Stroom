#include "platform/network/rpc/driver_stats.h"
#include "unity.h"
#include <loadcore.h>
#include <netman.h>
#include <string.h>

static void* entries[8];
static int   present;
static int   query_result;

/**
 * @brief Simulate the read-only Ethernet status query.
 * @param command Requested operation.
 * @param args Unused input.
 * @param args_size Input byte count.
 * @param output Destination.
 * @param output_size Destination size.
 * @return Configured driver result.
 */
static int query(unsigned int command, void* args, unsigned int args_size, void* output, unsigned int output_size)
{
    TEST_ASSERT_EQUAL_UINT(NETMAN_NETIF_IOCTL_ETH_GET_STATUS, command);
    TEST_ASSERT_NULL(args);
    TEST_ASSERT_EQUAL_UINT(0, args_size);
    TEST_ASSERT_EQUAL_UINT(sizeof(struct NetManEthStatus), output_size);

    struct NetManEthStatus* status = output;

    status->LinkStatus                     = 1;
    status->LinkMode                       = NETMAN_NETIF_ETH_LINK_MODE_100M_FDX;
    status->stats.RxAllocFail              = 65535;
    status->stats.RxFrameOverrunCount      = 2;
    status->stats.RxFrameBadLengthCount    = 3;
    status->stats.RxFrameBadFCSCount       = 4;
    status->stats.RxFrameBadAlignmentCount = 5;
    status->stats.RxDroppedFrameCount      = 123456;
    status->stats.RxErrorCount             = 654321;

    return query_result;
}

/**
 * @brief Resolve a compatible optional NETMAN library.
 * @param library Requested library.
 * @return Simulated export table, or NULL.
 */
void* QueryLibraryEntryTable(iop_library_t* library)
{
    TEST_ASSERT_EQUAL_STRING("netman", library->name);
    TEST_ASSERT_EQUAL_HEX16(0x0300, library->version);

    return present ? entries : NULL;
}

/**
 * @brief Restore a complete optional module.
 */
void setUp(void)
{
    present      = 1;
    query_result = 0;

    for (unsigned i = 0; i < 8; ++i)
    {
        entries[i] = (void*)query;
    }
}

/**
 * @brief No fixture resources require cleanup.
 */
void tearDown(void)
{}

/**
 * @brief Preserve driver values without misreporting unavailable or failed queries as success.
 */
static void counters(void)
{
    Ps2NetDriverStats stats;

    platform_net_driver_stats(&stats);
    TEST_ASSERT_EQUAL_INT(1, stats.status);
    TEST_ASSERT_EQUAL_UINT(1, stats.link_up);
    TEST_ASSERT_EQUAL_UINT(4, stats.link_mode);
    TEST_ASSERT_EQUAL_UINT(65535, stats.rx_alloc_fail);
    TEST_ASSERT_EQUAL_UINT(2, stats.rx_overrun);
    TEST_ASSERT_EQUAL_UINT(3, stats.rx_bad_length);
    TEST_ASSERT_EQUAL_UINT(4, stats.rx_bad_fcs);
    TEST_ASSERT_EQUAL_UINT(5, stats.rx_bad_alignment);
    TEST_ASSERT_EQUAL_UINT(123456, stats.rx_dropped);
    TEST_ASSERT_EQUAL_UINT(654321, stats.rx_errors);

    present = 0;

    platform_net_driver_stats(&stats);
    TEST_ASSERT_EQUAL_INT(0, stats.status);
    TEST_ASSERT_EQUAL_UINT(0, stats.rx_alloc_fail);

    present    = 1;
    entries[6] = NULL;

    platform_net_driver_stats(&stats);
    TEST_ASSERT_EQUAL_INT(0, stats.status);

    entries[6]   = (void*)query;
    query_result = -1;

    platform_net_driver_stats(&stats);
    TEST_ASSERT_EQUAL_INT(-1, stats.status);
    TEST_ASSERT_EQUAL_UINT(0, stats.rx_alloc_fail);
}

/**
 * @brief Run actual IOP counter-adapter logic with host library substitutes.
 * @return Failed test count.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(counters);

    return UNITY_END();
}
