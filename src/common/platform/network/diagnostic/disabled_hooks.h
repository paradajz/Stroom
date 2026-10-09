#pragma once

#include <stdint.h>

/* EE-native lwIP shares the patched source tree; the recorder belongs to IOP. */
#include "lwip/tcp.h"
#include "lwip/prot/tcp.h"

/**
 * @brief Ignore receive observations in EE-native builds without the IOP recorder.
 * @param pcb Unused TCP control block.
 * @param seq Unused sequence number.
 * @param ack Unused acknowledgement number.
 * @param length Unused payload length.
 * @param window Unused receive window.
 * @param flags Unused TCP flags.
 */
static inline void platform_net_diagnostic_rx(const struct tcp_pcb* pcb, uint32_t seq, uint32_t ack, unsigned length, unsigned window, unsigned flags)
{
    (void)pcb;
    (void)seq;
    (void)ack;
    (void)length;
    (void)window;
    (void)flags;
}

/**
 * @brief Ignore transmit observations in EE-native builds without the IOP recorder.
 * @param pcb Unused TCP control block.
 * @param header Unused TCP header.
 * @param available Unused available space.
 * @param result Unused send result.
 */
static inline void platform_net_diagnostic_tx(const struct tcp_pcb* pcb, const struct tcp_hdr* header, unsigned available, int result)
{
    (void)pcb;
    (void)header;
    (void)available;
    (void)result;
}

/**
 * @brief Ignore delivery observations in EE-native builds without the IOP recorder.
 * @param pcb Unused TCP control block.
 * @param length Unused payload length.
 * @param result Unused delivery result.
 */
static inline void platform_net_diagnostic_delivery(const struct tcp_pcb* pcb, unsigned length, int result)
{
    (void)pcb;
    (void)length;
    (void)result;
}

/* Recorder event IDs belong to the IOP contract and are not needed on EE. */
#define platform_net_diagnostic_failure(kind, value) ((void)(value))
