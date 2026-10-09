#pragma once

#include <stdint.h>

#define PS2_TCP_STATE_OPTION 0x5354
#define PS2_TCP_STATE_ABI    0x53544331u

/** Read-only TCP state, copied while lwIP holds its core lock. Fits the socket RPC's 128-byte option buffer. */
typedef struct
{
    uint32_t abi, flags, state;
    uint32_t last_ack, next_sequence, last_written;
    uint32_t send_space, queue_length;
    uint32_t unsent_segments, unsent_bytes, unsent_ref;
    uint32_t unacked_segments, unacked_bytes, unacked_ref;
    uint32_t congestion_window, peer_window;
    uint32_t retransmissions, rto_ms;
    int32_t  timer_ms;
    uint32_t persist_backoff;
} Ps2TcpState;

_Static_assert(sizeof(Ps2TcpState) <= 128, "TCP state must fit the existing socket RPC");
