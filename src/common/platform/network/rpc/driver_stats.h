#pragma once

#include <stdint.h>

/** Failure codes for this API. */
typedef enum
{
    PS2_NET_DRIVER_ERROR_STATUS = -1,
} Ps2NetDriverError;

#define PS2_SOCKET_RPC_DRIVER_STATS 128

/**
 * @brief Fixed-width Ethernet counters shared by the EE and IOP bridge.
 */
typedef struct
{
    int32_t  status;           /**< One on success, zero without compatible NETMAN, a negative Ps2NetDriverError on failure. */
    uint32_t link_up;          /**< Driver link state. */
    uint32_t link_mode;        /**< NETMAN negotiated link-mode value. */
    uint32_t rx_alloc_fail;    /**< Raw 16-bit receive allocation failure counter. */
    uint32_t rx_overrun;       /**< Raw 16-bit receive FIFO overrun counter. */
    uint32_t rx_bad_length;    /**< Raw 16-bit invalid frame-length counter. */
    uint32_t rx_bad_fcs;       /**< Raw 16-bit frame checksum failure counter. */
    uint32_t rx_bad_alignment; /**< Raw 16-bit frame alignment error counter. */
    uint32_t rx_dropped;       /**< Driver receive dropped-frame counter. */
    uint32_t rx_errors;        /**< Driver receive error counter. */
} Ps2NetDriverStats;

#if defined(STROOM_DIAGNOSTICS) && STROOM_DIAGNOSTICS
/**
 * @brief Read existing NETMAN driver counters without resetting or reconfiguring networking.
 * @param stats Destination; status indicates availability or failure.
 */
void platform_net_driver_stats(Ps2NetDriverStats* stats);
#endif
