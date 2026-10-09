#include "platform/network/tcp_state_hooks.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    struct pbuf      first = { .ref = 2 }, second = { .ref = 1 };
    struct tcp_seg   tail     = { .len = 1112, .p = &second };
    struct tcp_seg   head     = { .len = 1460, .p = &first, .next = &tail };
    struct tcp_pcb   pcb      = { .state = ESTABLISHED, .unsent = &head, .unacked = &tail, .snd_buf = 0, .lastack = 123, .snd_nxt = 456, .snd_lbb = 789, .cwnd = 1460, .snd_wnd = 65535, .nrtx = 2, .rto = 3, .rtime = 1 };
    struct netconn   conn     = { .type = NETCONN_TCP, .pcb.tcp = &pcb };
    struct lwip_sock socket   = { .conn = &conn };
    struct tcp_pcb   before   = pcb;
    Ps2TcpState      snapshot = { 0 };
    socklen_t        length   = sizeof(snapshot);
    int              error    = -1;

    assert(platform_tcp_state_option(0, &socket, IPPROTO_TCP, PS2_TCP_STATE_OPTION, &snapshot, &length, &error) == 1);
    assert(error == 0 && length == sizeof(snapshot) && snapshot.abi == PS2_TCP_STATE_ABI);
    assert(snapshot.unsent_segments == 2 && snapshot.unsent_bytes == 2572 && snapshot.unsent_ref == 2);
    assert(snapshot.unacked_segments == 1 && snapshot.unacked_bytes == 1112 && snapshot.unacked_ref == 1);
    assert(snapshot.rto_ms == 1500 && snapshot.timer_ms == 500 && snapshot.retransmissions == 2);
    assert(snapshot.last_ack == 123 && snapshot.next_sequence == 456 && snapshot.last_written == 789);
    assert(memcmp(&pcb, &before, sizeof(pcb)) == 0 && first.ref == 2 && second.ref == 1);
    assert(platform_tcp_state_option(0, &socket, IPPROTO_TCP, TCP_NODELAY, &snapshot, &length, &error) == 0);
    --length;
    assert(platform_tcp_state_option(0, &socket, IPPROTO_TCP, PS2_TCP_STATE_OPTION, &snapshot, &length, &error) == 1 && error == EINVAL);

    length    = sizeof(snapshot);
    pcb.state = LISTEN;

    assert(platform_tcp_state_option(0, &socket, IPPROTO_TCP, PS2_TCP_STATE_OPTION, &snapshot, &length, &error) == 1 && error == ENOTCONN);

    pcb.state = ESTABLISHED;
    pcb.rtime = -1;

    assert(platform_tcp_state_option(0, &socket, IPPROTO_TCP, PS2_TCP_STATE_OPTION, &snapshot, &length, &error) == 1 && snapshot.timer_ms == -1);
    puts("Read-only TCP state hook passed");

    return 0;
}
