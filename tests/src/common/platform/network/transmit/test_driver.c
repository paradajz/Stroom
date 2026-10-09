#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
#include "netif/etharp.h"
#include <netman.h>
#include <loadcore.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct netif*              interface;
static struct NetManNetProtStack* stack;
static unsigned                   notified, fail;
static struct pbuf                clone;
/* The initializer only takes this table's address; no IOP entries are invoked. */
char _exp_ps2ip[64];

int network_module_start(int argc, char* argv[]);

void platform_net_diagnostic_failure(unsigned kind, int value)
{
    (void)kind;
    (void)value;
}

int RegisterLibraryEntries(struct irx_export_table* table)
{
    (void)table;

    return 0;
}

void CpuSuspendIntr(int* state)
{
    *state = 0;
}

void CpuResumeIntr(int state)
{
    (void)state;
}

err_t sys_sem_new(sys_sem_t* sem, u8_t count)
{
    (void)count;

    *sem = 1;

    return ERR_OK;
}

void sys_sem_free(sys_sem_t* sem)
{
    *sem = 0;
}

void sys_sem_signal(sys_sem_t* sem)
{
    (void)sem;
}

u32_t sys_arch_sem_wait(sys_sem_t* sem, u32_t timeout)
{
    (void)sem;
    (void)timeout;

    return 0;
}

void tcpip_init(tcpip_init_done_fn done, void* arg)
{
    done(arg);
}

err_t tcpip_input(struct pbuf* p, struct netif* netif)
{
    (void)p;
    (void)netif;

    return ERR_OK;
}

err_t tcpip_callback(tcpip_callback_fn callback, void* arg)
{
    (void)callback;
    (void)arg;

    return ERR_OK;
}

void netif_set_link_up(struct netif* netif)
{
    (void)netif;
}

void netif_set_link_down(struct netif* netif)
{
    (void)netif;
}

void netif_set_default(struct netif* netif)
{
    (void)netif;
}

void netif_set_up(struct netif* netif)
{
    (void)netif;
}

u32_t lwip_htonl(u32_t value)
{
    return __builtin_bswap32(value);
}

u32_t ipaddr_addr(const char* address)
{
    (void)address;

    return 0;
}

err_t etharp_output(struct netif* netif, struct pbuf* p, const ip4_addr_t* address)
{
    (void)netif;
    (void)p;
    (void)address;

    return ERR_OK;
}

struct netif* netif_add(struct netif* netif, const ip4_addr_t* ip, const ip4_addr_t* mask, const ip4_addr_t* gateway, void* state, netif_init_fn init, netif_input_fn input)
{
    (void)ip;
    (void)mask;
    (void)gateway;
    (void)state;

    interface    = netif;
    netif->input = input;

    assert(init(netif) == ERR_OK);

    return netif;
}

int NetManRegisterNetworkStack(const struct NetManNetProtStack* callbacks)
{
    stack = (struct NetManNetProtStack*)callbacks;

    return 0;
}

void NetManNetIFXmit(void)
{
    ++notified;
}

int NetManIoctl(unsigned int command, void* args, unsigned int size, void* output, unsigned int output_size)
{
    (void)command;
    (void)args;
    (void)size;
    memset(output, 0, output_size);

    return 0;
}

struct pbuf* pbuf_alloc(pbuf_layer layer, u16_t bytes, pbuf_type type)
{
    (void)layer;
    (void)bytes;
    (void)type;

    return NULL;
}

void pbuf_ref(struct pbuf* p)
{
    ++p->ref;
}

u8_t pbuf_free(struct pbuf* p)
{
    assert(p->ref);
    --p->ref;

    return p->ref == 0;
}

struct pbuf* pbuf_coalesce(struct pbuf* p, pbuf_layer layer)
{
    (void)layer;

    if (fail)
    {
        return p;
    }

    pbuf_free(p);

    clone = (struct pbuf){ .len = p->tot_len, .tot_len = p->tot_len, .ref = 1, .payload = p->payload };

    return &clone;
}

int main(void)
{
    char* args[] = { "ps2ip" };

    assert(network_module_start(1, args) == MODULE_RESIDENT_END);
    assert(interface && interface->linkoutput && stack);

    char        bytes[100] = { 0 };
    struct pbuf tail       = { .len = 50, .tot_len = 50, .ref = 1 };
    struct pbuf packet     = { .len = 50, .tot_len = 100, .ref = 1, .next = &tail, .payload = bytes };
    void*       payload    = NULL;
    fail                   = 1;

    for (unsigned i = 0; i < 100; ++i)
    {
        assert(interface->linkoutput(interface, &packet) == ERR_MEM);
        assert(packet.ref == 1 && tail.ref == 1 && notified == 0);
        assert(stack->NextTxPacket(&payload) == 0);
    }

    fail = 0;

    assert(interface->linkoutput(interface, &packet) == ERR_OK);
    assert(packet.ref == 1 && clone.ref == 1 && notified == 1);
    assert(stack->NextTxPacket(&payload) == 100 && payload == bytes);
    stack->DeQTxPacket();
    assert(clone.ref == 0 && packet.ref == 1);
    assert(stack->NextTxPacket(&payload) == 0);

    packet.tot_len = 50;
    packet.len     = 50;
    packet.next    = NULL;

    assert(interface->linkoutput(interface, &packet) == ERR_OK);
    assert(packet.ref == 2 && notified == 2);
    assert(stack->NextTxPacket(&payload) == 50);
    stack->DeQTxPacket();
    assert(packet.ref == 1 && stack->NextTxPacket(&payload) == 0);
    puts("PS2 network transmit buffer ownership passed");

    return 0;
}
