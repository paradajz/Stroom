#pragma once

#include "platform/network/diagnostic/wire.h"
#include <stdint.h>

#define LWIP_TCP_SACK_OUT    1
#define TF_SACK              0x100
#define TCP_HLEN             20
#define TCPH_HDRLEN_BYTES(p) ((ntohs((p)->offset_flags) >> 12) * 4)
#define ip_2_ip4(p)          (p)
#define ip4_addr_get_u32(p)  (*(p))
#define TCPH_FLAGS(p)        (ntohs((p)->offset_flags) & 0x3f)

struct tcp_pcb
{
    unsigned local_port;
    unsigned remote_port;
    uint32_t remote_ip;
    uint32_t rcv_nxt;
    uint32_t rcv_wnd;
    unsigned flags;
};

struct tcp_hdr
{
    uint16_t src;
    uint16_t dest;
    uint32_t seqno;
    uint32_t ackno;
    uint16_t offset_flags;
    uint16_t wnd;
    uint16_t checksum;
    uint16_t urgent;
};

void platform_net_diagnostic_rx(const struct tcp_pcb*, uint32_t, uint32_t, unsigned, unsigned, unsigned);
void platform_net_diagnostic_tx(const struct tcp_pcb*, const struct tcp_hdr*, unsigned, int);
void platform_net_diagnostic_delivery(const struct tcp_pcb*, unsigned, int);
void platform_net_diagnostic_failure(unsigned, int);
void platform_net_diagnostic_service(const Ps2DiagnosticRequest*, Ps2DiagnosticReply*);
