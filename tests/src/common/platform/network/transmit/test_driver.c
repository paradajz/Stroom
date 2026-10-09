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

/**
 * @brief Substitute network_module_start in this host fixture.
 */
int network_module_start(int argc, char* argv[]);

/**
 * @brief Substitute platform_net_diagnostic_failure in this host fixture.
 */
void platform_net_diagnostic_failure(unsigned kind, int value)
{
    (void)kind;
    (void)value;
}

/**
 * @brief Substitute RegisterLibraryEntries in this host fixture.
 */
int RegisterLibraryEntries(struct irx_export_table* table)
{
    (void)table;

    return 0;
}

/**
 * @brief Substitute CpuSuspendIntr in this host fixture.
 */
void CpuSuspendIntr(int* state)
{
    *state = 0;
}

/**
 * @brief Substitute CpuResumeIntr in this host fixture.
 */
void CpuResumeIntr(int state)
{
    (void)state;
}

/**
 * @brief Substitute sys_sem_new in this host fixture.
 */
err_t sys_sem_new(sys_sem_t* sem, u8_t count)
{
    (void)count;

    *sem = 1;

    return ERR_OK;
}

/**
 * @brief Substitute sys_sem_free in this host fixture.
 */
void sys_sem_free(sys_sem_t* sem)
{
    *sem = 0;
}

/**
 * @brief Substitute sys_sem_signal in this host fixture.
 */
void sys_sem_signal(sys_sem_t* sem)
{
    (void)sem;
}

/**
 * @brief Substitute sys_arch_sem_wait in this host fixture.
 */
u32_t sys_arch_sem_wait(sys_sem_t* sem, u32_t timeout)
{
    (void)sem;
    (void)timeout;

    return 0;
}

/**
 * @brief Substitute tcpip_init in this host fixture.
 */
void tcpip_init(tcpip_init_done_fn done, void* arg)
{
    done(arg);
}

/**
 * @brief Substitute tcpip_input in this host fixture.
 */
err_t tcpip_input(struct pbuf* p, struct netif* netif)
{
    (void)p;
    (void)netif;

    return ERR_OK;
}

/**
 * @brief Substitute tcpip_callback in this host fixture.
 */
err_t tcpip_callback(tcpip_callback_fn callback, void* arg)
{
    (void)callback;
    (void)arg;

    return ERR_OK;
}

/**
 * @brief Substitute netif_set_link_up in this host fixture.
 */
void netif_set_link_up(struct netif* netif)
{
    (void)netif;
}

/**
 * @brief Substitute netif_set_link_down in this host fixture.
 */
void netif_set_link_down(struct netif* netif)
{
    (void)netif;
}

/**
 * @brief Substitute netif_set_default in this host fixture.
 */
void netif_set_default(struct netif* netif)
{
    (void)netif;
}

/**
 * @brief Substitute netif_set_up in this host fixture.
 */
void netif_set_up(struct netif* netif)
{
    (void)netif;
}

/**
 * @brief Substitute lwip_htonl in this host fixture.
 */
u32_t lwip_htonl(u32_t value)
{
    return __builtin_bswap32(value);
}

/**
 * @brief Substitute ipaddr_addr in this host fixture.
 */
u32_t ipaddr_addr(const char* address)
{
    (void)address;

    return 0;
}

/**
 * @brief Substitute etharp_output in this host fixture.
 */
err_t etharp_output(struct netif* netif, struct pbuf* p, const ip4_addr_t* address)
{
    (void)netif;
    (void)p;
    (void)address;

    return ERR_OK;
}

/**
 * @brief Substitute netif_add in this host fixture.
 */
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

/**
 * @brief Substitute NetManRegisterNetworkStack in this host fixture.
 */
int NetManRegisterNetworkStack(const struct NetManNetProtStack* callbacks)
{
    stack = (struct NetManNetProtStack*)callbacks;

    return 0;
}

/**
 * @brief Substitute NetManNetIFXmit in this host fixture.
 */
void NetManNetIFXmit(void)
{
    ++notified;
}

/**
 * @brief Substitute NetManIoctl in this host fixture.
 */
int NetManIoctl(unsigned int command, void* args, unsigned int size, void* output, unsigned int output_size)
{
    (void)command;
    (void)args;
    (void)size;
    memset(output, 0, output_size);

    return 0;
}

/**
 * @brief Substitute pbuf_alloc in this host fixture.
 */
struct pbuf* pbuf_alloc(pbuf_layer layer, u16_t bytes, pbuf_type type)
{
    (void)layer;
    (void)bytes;
    (void)type;

    return NULL;
}

/**
 * @brief Substitute pbuf_ref in this host fixture.
 */
void pbuf_ref(struct pbuf* p)
{
    ++p->ref;
}

/**
 * @brief Substitute pbuf_free in this host fixture.
 */
u8_t pbuf_free(struct pbuf* p)
{
    assert(p->ref);
    --p->ref;

    return p->ref == 0;
}

/**
 * @brief Substitute pbuf_coalesce in this host fixture.
 */
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

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
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
