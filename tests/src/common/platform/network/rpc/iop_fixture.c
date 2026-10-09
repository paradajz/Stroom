#include "rpc_fixture.h"
#include "platform/iop/services.h"
#include "platform/network/diagnostic/wire.h"
#include "platform/network/rpc/driver_stats.h"
#include <sifrpc.h>
#include <sifman.h>
#include <thbase.h>
#include <tcpip.h>
#include "unity.h"
#include <string.h>

/**
 * @brief Substitute socket_module_start in this host fixture.
 */
int socket_module_start(int argc, char** argv);
static void (*entry)(void*);
static void* (*handler)(unsigned, void*, int);
static void*         buffer;
static int           receive_result;
static int           link_ready;
static int           transfer_result;
static unsigned      sends;
static unsigned char sent[4096];

/**
 * @brief Substitute CreateThread in this host fixture.
 */
int CreateThread(iop_thread_t* thread)
{
    entry = thread->thread;

    return 9;
}

/**
 * @brief Substitute StartThread in this host fixture.
 */
int StartThread(int id, void* argument)
{
    TEST_ASSERT_EQUAL_INT(9, id);
    entry(argument);

    return 0;
}

/**
 * @brief Substitute GetThreadId in this host fixture.
 */
int GetThreadId(void)
{
    return 9;
}

/**
 * @brief Substitute sceSifSetRpcQueue in this host fixture.
 */
void sceSifSetRpcQueue(SifRpcDataQueue_t* queue, int thread)
{
    (void)queue;
    TEST_ASSERT_EQUAL_INT(9, thread);
}

/**
 * @brief Substitute sceSifRegisterRpc in this host fixture.
 */
void sceSifRegisterRpc(SifRpcServerData_t* server, unsigned id, void* (*callback)(unsigned, void*, int), void* storage, void* completion, void* argument, SifRpcDataQueue_t* queue)
{
    (void)server;
    (void)completion;
    (void)argument;
    (void)queue;
    TEST_ASSERT_EQUAL_HEX32(PS2_RPC_SOCKET, id);

    handler = callback;
    buffer  = storage;
}

/**
 * @brief Substitute sceSifRpcLoop in this host fixture.
 */
void sceSifRpcLoop(SifRpcDataQueue_t* queue)
{
    (void)queue;
}

/**
 * @brief Provide RPC peer open for the EE/IOP fixture.
 */
void rpc_peer_open(void)
{
    handler = NULL;

    TEST_ASSERT_EQUAL_INT(0, socket_module_start(0, NULL));
    TEST_ASSERT_NOT_NULL(handler);

    receive_result  = 0;
    link_ready      = 1;
    transfer_result = 0;
    sends           = 0;

    memset(sent, 0, sizeof(sent));
}

void rpc_peer_transfer(int result)
{
    transfer_result = result;
}

unsigned rpc_peer_sends(void)
{
    return sends;
}

const unsigned char* rpc_peer_sent(void)
{
    return sent;
}

/** @brief Copy a TCP send's payload into fixture-owned observation storage. */
ssize_t rpc_peer_send(int fd, const void* data, size_t size, int flags)
{
    TEST_ASSERT_EQUAL_INT(4, fd);
    (void)flags;
    TEST_ASSERT_LESS_OR_EQUAL_UINT(sizeof(sent), size);
    memcpy(sent, data, size);
    ++sends;

    return (ssize_t)size;
}

/** @brief Copy a UDP send's payload through the same observation path. */
ssize_t rpc_peer_sendto(int fd, const void* data, size_t size, int flags, const struct sockaddr* address, socklen_t length)
{
    (void)address;
    (void)length;

    return rpc_peer_send(fd, data, size, flags);
}

/**
 * @brief Provide RPC peer link for the EE/IOP fixture.
 */
void rpc_peer_link(int ready)
{
    link_ready = ready;
}

/**
 * @brief Substitute platform_net_link_ready in this host fixture.
 */
int platform_net_link_ready(void)
{
    return link_ready;
}

/**
 * @brief Provide RPC peer call for the EE/IOP fixture.
 */
void* rpc_peer_call(unsigned command, void* request, int size)
{
    memcpy(buffer, request, (size_t)size);

    return handler(command, buffer, size);
}

/**
 * @brief Provide RPC peer receive for the EE/IOP fixture.
 */
void rpc_peer_receive(int result)
{
    receive_result = result;
}

/**
 * @brief Provide RPC peer recv for the EE/IOP fixture.
 */
ssize_t rpc_peer_recv(int fd, void* data, size_t size, int flags)
{
    TEST_ASSERT_EQUAL_INT(4, fd);
    (void)flags;

    if (receive_result > 0)
    {
        TEST_ASSERT_LESS_OR_EQUAL_UINT(size, (unsigned)receive_result);
        memset(data, 'A', (size_t)receive_result);
    }

    return receive_result;
}

/**
 * @brief Provide RPC peer recvfrom for the EE/IOP fixture.
 */
ssize_t rpc_peer_recvfrom(int fd, void* data, size_t size, int flags, struct sockaddr* address, socklen_t* length)
{
    memset(address, 0, sizeof(*address));

    address->sa_family = AF_INET;
    *length            = sizeof(*address);

    return rpc_peer_recv(fd, data, size, flags);
}

/**
 * @brief Provide RPC peer socket for the EE/IOP fixture.
 */
int rpc_peer_socket(int domain, int type, int protocol)
{
    (void)domain;
    (void)type;
    (void)protocol;

    return 4;
}

/**
 * @brief Substitute sceSifSetDma in this host fixture.
 */
int sceSifSetDma(struct t_SifDmaTransfer* transfer, int count)
{
    TEST_ASSERT_EQUAL_INT(1, count);
    memcpy(transfer->dest, transfer->src, (size_t)transfer->size);

    return 1;
}

/**
 * @brief Substitute sceSifDmaStat in this host fixture.
 */
int sceSifDmaStat(int id)
{
    (void)id;

    return -1;
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
 * @brief Substitute disconnect in this host fixture.
 */
int disconnect(int fd)
{
    (void)fd;

    return 0;
}

/**
 * @brief Substitute ioctlsocket in this host fixture.
 */
int ioctlsocket(int fd, long command, void* argument)
{
    (void)fd;
    (void)command;
    (void)argument;

    return 0;
}

/**
 * @brief Substitute ps2ip_getconfig in this host fixture.
 */
int ps2ip_getconfig(char* name, t_ip_info* info)
{
    (void)name;
    memset(info, 0, sizeof(*info));

    return 1;
}

/**
 * @brief Substitute ps2ip_setconfig in this host fixture.
 */
int ps2ip_setconfig(t_ip_info* info)
{
    (void)info;

    return 1;
}

/**
 * @brief Substitute dns_setserver in this host fixture.
 */
void dns_setserver(u8 index, const ip_addr_t* address)
{
    (void)index;
    (void)address;
}

/**
 * @brief Substitute dns_getserver in this host fixture.
 */
const ip_addr_t* dns_getserver(u8 index)
{
    static const ip_addr_t address;

    (void)index;

    return &address;
}

/**
 * @brief Substitute platform_net_tx_stats in this host fixture.
 */
void platform_net_tx_stats(Ps2NetTxStats* stats)
{
    memset(stats, 0, sizeof(*stats));

    stats->abi          = PS2_NET_TX_STATS_ABI;
    stats->status       = 1;
    stats->tx_underruns = 37;
}

#if STROOM_DIAGNOSTICS
/**
 * @brief Substitute platform_net_driver_stats in this host fixture.
 */
void platform_net_driver_stats(Ps2NetDriverStats* stats)
{
    memset(stats, 0, sizeof(*stats));
}

/**
 * @brief Substitute platform_net_diagnostic_read in this host fixture.
 */
void platform_net_diagnostic_read(int socket, int result, int complete)
{
    (void)socket;
    (void)result;
    (void)complete;
}

/**
 * @brief Substitute platform_net_diagnostic in this host fixture.
 */
void platform_net_diagnostic(const Ps2DiagnosticRequest* request, Ps2DiagnosticReply* reply)
{
    (void)request;
    memset(reply, 0, sizeof(*reply));

    reply->status = 1;
    reply->abi    = DIAGNOSTIC_IOP_ABI;
}
#endif
/**
 * @brief Substitute sceSifGetOtherData in this host fixture.
 */
int sceSifGetOtherData(SifRpcReceiveData_t* receive, void* source, void* destination, int size, int mode)
{
    (void)receive;
    (void)mode;

    if (transfer_result < 0)
    {
        return transfer_result;
    }

    memcpy(destination, source, (size_t)size);

    return 0;
}
