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

int socket_module_start(int argc, char** argv);
static void (*entry)(void*);
static void* (*handler)(unsigned, void*, int);
static void* buffer;
static int   receive_result;

int CreateThread(iop_thread_t* thread)
{
    entry = thread->thread;

    return 9;
}

int StartThread(int id, void* argument)
{
    TEST_ASSERT_EQUAL_INT(9, id);
    entry(argument);

    return 0;
}

int GetThreadId(void)
{
    return 9;
}

void sceSifSetRpcQueue(SifRpcDataQueue_t* queue, int thread)
{
    (void)queue;
    TEST_ASSERT_EQUAL_INT(9, thread);
}

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

void sceSifRpcLoop(SifRpcDataQueue_t* queue)
{
    (void)queue;
}

void rpc_peer_open(void)
{
    handler = NULL;

    TEST_ASSERT_EQUAL_INT(0, socket_module_start(0, NULL));
    TEST_ASSERT_NOT_NULL(handler);

    receive_result = 0;
}

void* rpc_peer_call(unsigned command, void* request, int size)
{
    memcpy(buffer, request, (size_t)size);

    return handler(command, buffer, size);
}

void rpc_peer_receive(int result)
{
    receive_result = result;
}

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

ssize_t rpc_peer_recvfrom(int fd, void* data, size_t size, int flags, struct sockaddr* address, socklen_t* length)
{
    memset(address, 0, sizeof(*address));

    address->sa_family = AF_INET;
    *length            = sizeof(*address);

    return rpc_peer_recv(fd, data, size, flags);
}

int rpc_peer_socket(int domain, int type, int protocol)
{
    (void)domain;
    (void)type;
    (void)protocol;

    return 4;
}

int sceSifSetDma(struct t_SifDmaTransfer* transfer, int count)
{
    TEST_ASSERT_EQUAL_INT(1, count);
    memcpy(transfer->dest, transfer->src, (size_t)transfer->size);

    return 1;
}

int sceSifDmaStat(int id)
{
    (void)id;

    return -1;
}

void CpuSuspendIntr(int* state)
{
    *state = 0;
}

void CpuResumeIntr(int state)
{
    (void)state;
}

int disconnect(int fd)
{
    (void)fd;

    return 0;
}

int ioctlsocket(int fd, long command, void* argument)
{
    (void)fd;
    (void)command;
    (void)argument;

    return 0;
}

int ps2ip_getconfig(char* name, t_ip_info* info)
{
    (void)name;
    memset(info, 0, sizeof(*info));

    return 1;
}

int ps2ip_setconfig(t_ip_info* info)
{
    (void)info;

    return 1;
}

void dns_setserver(u8 index, const ip_addr_t* address)
{
    (void)index;
    (void)address;
}

const ip_addr_t* dns_getserver(u8 index)
{
    static const ip_addr_t address;

    (void)index;

    return &address;
}

#if STROOM_DIAGNOSTICS
void platform_net_driver_stats(Ps2NetDriverStats* stats)
{
    memset(stats, 0, sizeof(*stats));
}

void platform_net_diagnostic_read(int socket, int result, int complete)
{
    (void)socket;
    (void)result;
    (void)complete;
}

void platform_net_diagnostic(const Ps2DiagnosticRequest* request, Ps2DiagnosticReply* reply)
{
    (void)request;
    memset(reply, 0, sizeof(*reply));

    reply->status = 1;
    reply->abi    = DIAGNOSTIC_IOP_ABI;
}
#endif
int sceSifGetOtherData(SifRpcReceiveData_t* receive, void* source, void* destination, int size, int mode)
{
    (void)receive;
    (void)mode;
    memcpy(destination, source, (size_t)size);

    return 0;
}
