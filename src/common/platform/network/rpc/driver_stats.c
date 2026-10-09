#include "platform/network/rpc/driver_stats.h"
#include <loadcore.h>
#include <netman.h>
#include <string.h>

#define NETMAN_LIBRARY_VERSION 0x0300
#define NETMAN_IOCTL_EXPORT    7

/**
 * @brief Signature of the optional NETMAN status entry point.
 */
typedef int (*NetmanIoctl)(unsigned int command, void* args, unsigned int args_size, void* output, unsigned int output_size);

/**
 * @brief Locate the optional NETMAN ioctl export after validating its table.
 * @return Resident ioctl function, or NULL when the library or export is unavailable.
 */
static NetmanIoctl netman_ioctl(void)
{
    iop_library_t library = { .version = NETMAN_LIBRARY_VERSION, .name = "netman" };
    void**        exports = QueryLibraryEntryTable(&library);

    if (!exports)
    {
        return NULL;
    }

    /* Export tables terminate with NULL; reject incomplete compatible tables. */

    for (unsigned i = 0; i <= NETMAN_IOCTL_EXPORT; ++i)
    {
        if (!exports[i])
        {
            return NULL;
        }
    }

    return (NetmanIoctl)exports[NETMAN_IOCTL_EXPORT];
}

/**
 * @brief Read existing Ethernet status through optional NETMAN support.
 * @param status Destination for the status query.
 * @return One on success, zero without support, PS2_NET_DRIVER_ERROR_STATUS on query failure.
 */
static int ethernet_status(struct NetManEthStatus* status)
{
    NetmanIoctl query = netman_ioctl();

    if (!query)
    {
        return 0;
    }

    return query(NETMAN_NETIF_IOCTL_ETH_GET_STATUS, NULL, 0, status, sizeof(*status)) < 0 ? PS2_NET_DRIVER_ERROR_STATUS : 1;
}

int platform_net_link_ready(void)
{
    NetmanIoctl query = netman_ioctl();

    return query && query(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP;
}

void platform_net_tx_stats(Ps2NetTxStats* stats)
{
    memset(stats, 0, sizeof(*stats));

    struct NetManEthStatus status = { 0 };

    stats->abi    = PS2_NET_TX_STATS_ABI;
    stats->status = ethernet_status(&status);

    if (stats->status != 1)
    {
        return;
    }

    stats->link_mode     = status.LinkMode;
    stats->tx_dropped    = status.stats.TxDroppedFrameCount;
    stats->tx_errors     = status.stats.TxErrorCount;
    stats->tx_carrier    = status.stats.TxFrameLOSSCRCount;
    stats->tx_deferred   = status.stats.TxFrameEDEFERCount;
    stats->tx_collisions = status.stats.TxFrameCollisionCount;
    stats->tx_underruns  = status.stats.TxFrameUnderrunCount;
    stats->rx_alloc_fail = status.stats.RxAllocFail;
    stats->rx_overrun    = status.stats.RxFrameOverrunCount;
    stats->rx_bad_fcs    = status.stats.RxFrameBadFCSCount;
}

#if STROOM_DIAGNOSTICS
void platform_net_driver_stats(Ps2NetDriverStats* stats)
{
    memset(stats, 0, sizeof(*stats));

    struct NetManEthStatus status = { 0 };

    stats->status = ethernet_status(&status);

    if (stats->status != 1)
    {
        return;
    }

    stats->link_up          = status.LinkStatus;
    stats->link_mode        = status.LinkMode;
    stats->rx_alloc_fail    = status.stats.RxAllocFail;
    stats->rx_overrun       = status.stats.RxFrameOverrunCount;
    stats->rx_bad_length    = status.stats.RxFrameBadLengthCount;
    stats->rx_bad_fcs       = status.stats.RxFrameBadFCSCount;
    stats->rx_bad_alignment = status.stats.RxFrameBadAlignmentCount;
    stats->rx_dropped       = status.stats.RxDroppedFrameCount;
    stats->rx_errors        = status.stats.RxErrorCount;
}
#endif
