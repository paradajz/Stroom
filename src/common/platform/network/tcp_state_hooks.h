#pragma once

#include "tcp_state_wire.h"
#include "lwip/api.h"
#include "lwip/priv/sockets_priv.h"
#include "lwip/priv/tcp_priv.h"
#include <errno.h>
#include <string.h>

/** lwIP socket hook, invoked under the core lock; it does not send, allocate or change TCP state. */
static inline int platform_tcp_state_option(int fd, struct lwip_sock* socket, int level, int option, void* output, socklen_t* length, int* error)
{
    (void)fd;

    if (level != IPPROTO_TCP || option != PS2_TCP_STATE_OPTION)
    {
        return 0;
    }

    *error = EINVAL;

    if (!output || !length || *length < sizeof(Ps2TcpState))
    {
        return 1;
    }

    *error = ENOTCONN;

    if (!socket || !socket->conn || NETCONNTYPE_GROUP(socket->conn->type) != NETCONN_TCP || !socket->conn->pcb.tcp)
    {
        return 1;
    }

    const struct tcp_pcb* pcb = socket->conn->pcb.tcp;

    if (pcb->state == LISTEN)
    {
        return 1;
    }

    Ps2TcpState state = {
        .abi               = PS2_TCP_STATE_ABI,
        .flags             = pcb->flags,
        .state             = pcb->state,
        .last_ack          = pcb->lastack,
        .next_sequence     = pcb->snd_nxt,
        .last_written      = pcb->snd_lbb,
        .send_space        = pcb->snd_buf,
        .queue_length      = pcb->snd_queuelen,
        .unsent_ref        = pcb->unsent ? pcb->unsent->p->ref : 0,
        .unacked_ref       = pcb->unacked ? pcb->unacked->p->ref : 0,
        .congestion_window = pcb->cwnd,
        .peer_window       = pcb->snd_wnd,
        .retransmissions   = pcb->nrtx,
        .rto_ms            = (uint32_t)pcb->rto * TCP_SLOW_INTERVAL,
        .timer_ms          = pcb->rtime < 0 ? -1 : (int32_t)pcb->rtime * TCP_SLOW_INTERVAL,
        .persist_backoff   = pcb->persist_backoff,
    };
    const struct tcp_seg* segment = pcb->unsent;

    while (segment && state.unsent_segments < 64)
    {
        ++state.unsent_segments;

        state.unsent_bytes += segment->len;
        segment = segment->next;
    }

    segment = pcb->unacked;

    while (segment && state.unacked_segments < 64)
    {
        ++state.unacked_segments;

        state.unacked_bytes += segment->len;
        segment = segment->next;
    }

    memcpy(output, &state, sizeof(state));

    *length = sizeof(state);
    *error  = 0;

    return 1;
}

#define LWIP_HOOK_SOCKETS_GETSOCKOPT platform_tcp_state_option
