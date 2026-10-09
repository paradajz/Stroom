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

void platform_net_driver_stats(Ps2NetDriverStats* stats)
{
    memset(stats, 0, sizeof(*stats));

    iop_library_t library = { .version = NETMAN_LIBRARY_VERSION, .name = "netman" };
    void**        exports = QueryLibraryEntryTable(&library);

    if (!exports)
    {
        return;
    }

    /* Export tables terminate with NULL; reject incomplete compatible tables. */

    for (unsigned i = 0; i <= NETMAN_IOCTL_EXPORT; ++i)
    {
        if (!exports[i])
        {
            return;
        }
    }

    NetmanIoctl            query  = (NetmanIoctl)exports[NETMAN_IOCTL_EXPORT];
    struct NetManEthStatus status = { 0 };

    if (query(NETMAN_NETIF_IOCTL_ETH_GET_STATUS, NULL, 0, &status, sizeof(status)) < 0)
    {
        stats->status = PS2_NET_DRIVER_ERROR_STATUS;

        return;
    }

    stats->status           = 1;
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
