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

/**
 * @brief Provide RPC fixture lifecycle for the EE/IOP fixture.
 */
void rpc_fixture_lifecycle(int creation, int deletion, int reboot)
{
    create_result = creation;
    delete_result = deletion;
    rebooted      = reboot;
    deletions     = 0;
    creations     = 0;
}

/**
 * @brief Provide RPC fixture creations for the EE/IOP fixture.
 */
int rpc_fixture_creations(void)
{
    return creations;
}

/**
 * @brief Provide RPC fixture deletions for the EE/IOP fixture.
 */
int rpc_fixture_deletions(void)
{
    return deletions;
}

/**
 * @brief Substitute HasIopRebootedSinceLastCall in this host fixture.
 */
int HasIopRebootedSinceLastCall(void)
{
    int result = rebooted;

    rebooted = 0;

    return result;
}

/**
 * @brief Substitute CreateSema in this host fixture.
 */
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

/**
 * @brief Substitute DeleteSema in this host fixture.
 */
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

/**
 * @brief Substitute WaitSema in this host fixture.
 */
int WaitSema(int id)
{
    TEST_ASSERT_EQUAL_INT(live_handle, id);
    TEST_ASSERT_TRUE(id >= 0);
    TEST_ASSERT_EQUAL_INT(0, locks);
    ++locks;

    return 0;
}

/**
 * @brief Substitute SignalSema in this host fixture.
 */
int SignalSema(int id)
{
    TEST_ASSERT_EQUAL_INT(live_handle, id);
    TEST_ASSERT_TRUE(id >= 0);
    TEST_ASSERT_EQUAL_INT(1, locks);
    --locks;

    return 0;
}

/**
 * @brief Substitute nopdelay in this host fixture.
 */
void nopdelay(void)
{}

/**
 * @brief Substitute sceSifWriteBackDCache in this host fixture.
 */
void sceSifWriteBackDCache(void* data, int size)
{
    (void)data;
    (void)size;
}

/**
 * @brief Substitute sceSifBindRpc in this host fixture.
 */
int sceSifBindRpc(SifRpcClientData_t* client, unsigned id, int mode)
{
    TEST_ASSERT_EQUAL_HEX32(PS2_RPC_SOCKET, id);
    TEST_ASSERT_EQUAL_INT(0, mode);

    client->server = client;

    return 0;
}

/**
 * @brief Substitute __fdman_get_new_descriptor in this host fixture.
 */
int __fdman_get_new_descriptor(void)
{
    memset(&descriptor, 0, sizeof(descriptor));

    __descriptormap[17] = &descriptor;

    return 17;
}

/**
 * @brief Substitute __fdman_release_descriptor in this host fixture.
 */
void __fdman_release_descriptor(int fd)
{
    __descriptormap[fd] = NULL;
}

/**
 * @brief Substitute __transform_errno in this host fixture.
 */
int __transform_errno(int result)
{
    if (result < 0)
    {
        errno = -result;

        return -1;
    }

    return result;
}

/**
 * @brief Substitute ps2sdk_get_iop_fd in this host fixture.
 */
int ps2sdk_get_iop_fd(int fd)
{
    if (fd < 0 || fd >= __FILENO_MAX || !__descriptormap[fd])
    {
        return -1;
    }

    return __descriptormap[fd]->info.ops->getfd(__descriptormap[fd]->info.userdata);
}

/**
 * @brief Substitute strlcpy in this host fixture.
 */
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

/**
 * @brief Provide RPC fixture assert locked for the EE/IOP fixture.
 */
void rpc_fixture_assert_locked(void)
{
    TEST_ASSERT_EQUAL_INT(1, locks);
}
