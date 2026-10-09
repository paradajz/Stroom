#include "lwip/priv/api_msg.h"
#include "lwip/priv/tcpip_priv.h"
#include "lwip/tcp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned outputs;
static err_t    write_error;
sys_mutex_t     lock_tcpip_core;

err_t netconn_err(struct netconn* conn)
{
    (void)conn;

    return ERR_OK;
}

void sys_mutex_lock(sys_mutex_t* mutex)
{
    (void)mutex;
}

void sys_mutex_unlock(sys_mutex_t* mutex)
{
    (void)mutex;
}

void sys_sem_signal(sys_sem_t* sem)
{
    (void)sem;
}

u32_t sys_arch_sem_wait(sys_sem_t* sem, u32_t timeout)
{
    (void)sem;
    (void)timeout;
    abort();
}

err_t tcp_write(struct tcp_pcb* pcb, const void* data, u16_t bytes, u8_t flags)
{
    (void)data;
    (void)flags;

    if (write_error)
    {
        return write_error;
    }

    assert(bytes <= pcb->snd_buf);

    pcb->snd_buf -= bytes;

    return ERR_OK;
}

err_t tcp_output(struct tcp_pcb* pcb)
{
    ++outputs;

    pcb->snd_buf = TCP_SND_BUF;

    return ERR_OK;
}

int main(void)
{
    struct tcp_pcb pcb  = { 0 };
    struct netconn conn = { .type = NETCONN_TCP, .pcb.tcp = &pcb };

    netconn_set_nonblocking(&conn, 1);

    const char       samples[100] = { 0 };
    struct netvector vector       = { .ptr = samples, .len = sizeof(samples) };
    struct api_msg   msg          = { .conn = &conn, .msg.w = { .vector = &vector, .vector_cnt = 1, .len = sizeof(samples), .apiflags = NETCONN_COPY } };

    /* Previously accepted bytes fill snd_buf after a temporary output failure.
       No further ACK can arrive: every nonblocking retry must attempt output. */
    lwip_netconn_do_write(&msg);
    assert(outputs == 1);
    assert(msg.err == ERR_WOULDBLOCK);
    assert(msg.msg.w.offset == 0);
    assert(conn.state == NETCONN_NONE && conn.current_msg == NULL);
    lwip_netconn_do_write(&msg);
    assert(msg.err == ERR_OK && msg.msg.w.offset == sizeof(samples));

    /* Preserve an earlier partial write; retry output without consuming bytes. */
    pcb.snd_buf          = 0;
    msg.msg.w.vector_off = 50;
    msg.msg.w.offset     = 50;
    msg.msg.w.vector_cnt = 1;
    unsigned before      = outputs;

    lwip_netconn_do_write(&msg);
    assert(outputs == before + 1 && msg.err == ERR_OK);
    assert(msg.msg.w.offset == 50 && msg.msg.w.vector_off == 50);

    /* Allocation failures already retry output; retain that behavior. */
    write_error          = ERR_MEM;
    msg.msg.w.vector_off = 0;
    msg.msg.w.offset     = 0;
    before               = outputs;

    lwip_netconn_do_write(&msg);
    assert(outputs == before + 1 && msg.err == ERR_WOULDBLOCK);
    assert(msg.msg.w.offset == 0);
    puts("lwIP nonblocking transmit recovery passed");

    return 0;
}
