#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

const ip_addr_t ip_addr_any = { 0 };

/* Binding starts the TCP timer; this test advances no network timers. */
void tcp_timer_needed(void)
{
}

int main(void)
{
    /* Use the actual SDK options, so removing the patch fails this check. */
    assert(SO_REUSE == 1);

    const u16_t    port     = 49152;
    struct tcp_pcb previous = { .state = TIME_WAIT, .local_port = port };
    struct tcp_pcb listener = { 0 };

    tcp_tw_pcbs = &previous;

    /* Without the socket option, the old connection still reserves the port. */
    assert(tcp_bind(&listener, IP4_ADDR_ANY, port) == ERR_USE);
    assert(tcp_bound_pcbs == NULL);

    /* The CD server sets this option before rebinding after each disc change. */
    ip_set_option(&listener, SOF_REUSEADDR);
    assert(tcp_bind(&listener, IP4_ADDR_ANY, port) == ERR_OK);
    assert(tcp_bound_pcbs == &listener);
    assert(tcp_tw_pcbs == &previous);
    assert(previous.state == TIME_WAIT);
    assert(previous.local_port == port);

    tcp_tw_pcbs    = NULL;
    tcp_bound_pcbs = NULL;

    /* An accepted connection can still be awaiting its peer's FIN on ejection. */
    previous.state  = FIN_WAIT_2;
    tcp_active_pcbs = &previous;

    ip_set_option(&previous, SOF_REUSEADDR);
    memset(&listener, 0, sizeof(listener));
    ip_set_option(&listener, SOF_REUSEADDR);
    assert(tcp_bind(&listener, IP4_ADDR_ANY, port) == ERR_OK);
    assert(tcp_active_pcbs == &previous);
    assert(previous.state == FIN_WAIT_2);

    tcp_bound_pcbs = NULL;

    /* A different live socket that did not opt into reuse still blocks binding. */
    ip_reset_option(&previous, SOF_REUSEADDR);
    memset(&listener, 0, sizeof(listener));
    ip_set_option(&listener, SOF_REUSEADDR);
    assert(tcp_bind(&listener, IP4_ADDR_ANY, port) == ERR_USE);
    assert(tcp_bound_pcbs == NULL);
    puts("lwIP listener restart with closing CD connections passed");

    return 0;
}
