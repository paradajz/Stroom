#include <ps2sdkapi.h>
#include <kernel.h>
#include <sifrpc.h>
#include <fdman.h>
#include "platform/iop/services.h"
#include "unity.h"
#include <string.h>

_libcglue_fdman_socket_ops_t* _libcglue_fdman_socket_ops;
/* Substitute descriptor allocation; this storage belongs to the fixture. */
__descriptormap_type*       __descriptormap[__FILENO_MAX];
static __descriptormap_type descriptor;
static int                  locks;
static int                  live_handle   = -1;
static int                  create_result = 7, delete_result, rebooted;
static int                  creations, deletions;

void rpc_fixture_lifecycle(int creation, int deletion, int reboot)
{
    create_result = creation;
    delete_result = deletion;
    rebooted      = reboot;
    creations = deletions = 0;
}

int rpc_fixture_creations(void)
{
    return creations;
}

int rpc_fixture_deletions(void)
{
    return deletions;
}

int HasIopRebootedSinceLastCall(void)
{
    int result = rebooted;

    rebooted = 0;

    return result;
}

int CreateSema(ee_sema_t* sema)
{
    (void)sema;

    ++creations;

    if (create_result >= 0)
    {
        TEST_ASSERT_EQUAL_INT(-1, live_handle);

        live_handle = create_result;
    }

    return create_result;
}

int DeleteSema(int id)
{
    TEST_ASSERT_EQUAL_INT(live_handle, id);
    TEST_ASSERT_TRUE(id >= 0);
    TEST_ASSERT_EQUAL_INT(0, locks);

    ++deletions;

    if (delete_result >= 0)
    {
        live_handle = -1;
    }

    return delete_result;
}

int WaitSema(int id)
{
    TEST_ASSERT_EQUAL_INT(live_handle, id);
    TEST_ASSERT_TRUE(id >= 0);
    TEST_ASSERT_EQUAL_INT(0, locks);
    ++locks;

    return 0;
}

int SignalSema(int id)
{
    TEST_ASSERT_EQUAL_INT(live_handle, id);
    TEST_ASSERT_TRUE(id >= 0);
    TEST_ASSERT_EQUAL_INT(1, locks);
    --locks;

    return 0;
}

void nopdelay(void)
{}

void sceSifWriteBackDCache(void* data, int size)
{
    (void)data;
    (void)size;
}

int sceSifBindRpc(SifRpcClientData_t* client, unsigned id, int mode)
{
    TEST_ASSERT_EQUAL_HEX32(PS2_RPC_SOCKET, id);
    TEST_ASSERT_EQUAL_INT(0, mode);

    client->server = client;

    return 0;
}

int __fdman_get_new_descriptor(void)
{
    memset(&descriptor, 0, sizeof(descriptor));

    __descriptormap[17] = &descriptor;

    return 17;
}

void __fdman_release_descriptor(int fd)
{
    __descriptormap[fd] = NULL;
}

int __transform_errno(int result)
{
    if (result < 0)
    {
        errno = -result;

        return -1;
    }

    return result;
}

int ps2sdk_get_iop_fd(int fd)
{
    if (fd < 0 || fd >= __FILENO_MAX || !__descriptormap[fd])
    {
        return -1;
    }

    return __descriptormap[fd]->info.ops->getfd(__descriptormap[fd]->info.userdata);
}

size_t strlcpy(char* destination, const char* source, size_t capacity)
{
    size_t length = strlen(source);

    if (capacity)
    {
        size_t count = length < capacity - 1 ? length : capacity - 1;

        memcpy(destination, source, count);

        destination[count] = 0;
    }

    return length;
}

void rpc_fixture_assert_locked(void)
{
    TEST_ASSERT_EQUAL_INT(1, locks);
}
