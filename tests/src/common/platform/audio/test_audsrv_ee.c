#include <audsrv.h>
#include <kernel.h>
#include <sifrpc.h>
#include "unity.h"

enum
{
    FAIL_NONE,
    FAIL_WAIT,
    FAIL_RPC,
    FAIL_REPLY,
    FAIL_TERMINATE,
    FAIL_UNREGISTER,
    FAIL_QUEUE,
    FAIL_THREAD_DELETE,
    FAIL_SEMA_DELETE,
    FAIL_THREAD_CREATE,
    FAIL_THREAD_START
};

static int fault;
static int live_thread, live_sema, locked;
static int quit_calls, wait_calls, terminate_calls, thread_deletes, sema_deletes;
static int creates, unregisters, queue_removals;
static int iop_rebooted;
static void (*thread_entry)(void*);
void* _gp;

int HasIopRebootedSinceLastCall(void)
{
    int rebooted = iop_rebooted;

    iop_rebooted = 0;

    return rebooted;
}

void nopdelay(void)
{}

int SifInitIopHeap(void)
{
    return 0;
}

int GetThreadId(void)
{
    return 31;
}

int CreateSema(ee_sema_t* sema)
{
    TEST_ASSERT_EQUAL_INT(1, sema->init_count);
    TEST_ASSERT_FALSE(live_sema);

    live_sema = 1;

    ++creates;

    return 21;
}

int CreateThread(ee_thread_t* thread)
{
    if (fault == FAIL_THREAD_CREATE)
    {
        return -70;
    }

    TEST_ASSERT_FALSE(live_thread);

    live_thread  = 1;
    thread_entry = thread->func;

    return 31;
}

int StartThread(int thread, void* argument)
{
    TEST_ASSERT_EQUAL_INT(31, thread);

    if (fault == FAIL_THREAD_START)
    {
        return -71;
    }

    thread_entry(argument);

    return 0;
}

int WaitSema(int sema)
{
    TEST_ASSERT_EQUAL_INT(21, sema);
    ++wait_calls;

    if (fault == FAIL_WAIT)
    {
        return -72;
    }

    TEST_ASSERT_FALSE(locked);

    locked = 1;

    return 0;
}

int SignalSema(int sema)
{
    TEST_ASSERT_EQUAL_INT(21, sema);
    TEST_ASSERT_TRUE(locked);

    locked = 0;

    return 0;
}

int TerminateThread(int thread)
{
    TEST_ASSERT_EQUAL_INT(31, thread);
    TEST_ASSERT_TRUE(live_thread);
    ++terminate_calls;

    return fault == FAIL_TERMINATE ? -73 : 0;
}

int DeleteThread(int thread)
{
    TEST_ASSERT_EQUAL_INT(31, thread);
    TEST_ASSERT_TRUE(live_thread);
    ++thread_deletes;

    if (fault == FAIL_THREAD_DELETE)
    {
        return -74;
    }

    live_thread = 0;

    return 0;
}

int DeleteSema(int sema)
{
    TEST_ASSERT_EQUAL_INT(21, sema);
    TEST_ASSERT_TRUE(live_sema);
    ++sema_deletes;

    if (fault == FAIL_SEMA_DELETE)
    {
        return -75;
    }

    live_sema = locked = 0;

    return 0;
}

int sceSifBindRpc(SifRpcClientData_t* client, int id, int mode)
{
    TEST_ASSERT_EQUAL_INT(AUDSRV_IRX, id);
    TEST_ASSERT_EQUAL_INT(0, mode);

    client->server = client;

    return 0;
}

int sceSifCallRpc(SifRpcClientData_t* client, int command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument)
{
    (void)input;
    (void)input_size;
    (void)output_size;
    (void)callback;
    (void)argument;
    TEST_ASSERT_NOT_NULL(client->server);
    TEST_ASSERT_EQUAL_INT(0, mode);

    if (command == 1)
    {
        ++quit_calls;

        if (fault == FAIL_RPC)
        {
            return -76;
        }

        *(int*)output = fault == FAIL_REPLY ? 7 : 0;
    }
    else
    {
        TEST_ASSERT_EQUAL_INT(0, command);
        *(int*)output = 0;
    }

    return 0;
}

void sceSifSetRpcQueue(SifRpcDataQueue_t* queue, int thread)
{
    (void)queue;
    TEST_ASSERT_EQUAL_INT(31, thread);
}

void sceSifRegisterRpc(SifRpcServerData_t* server, int id, void* (*handler)(int, void*, int), void* buffer, void* callback, void* callback_buffer, SifRpcDataQueue_t* queue)
{
    (void)server;
    (void)handler;
    (void)buffer;
    (void)callback;
    (void)callback_buffer;
    (void)queue;
    TEST_ASSERT_EQUAL_INT(AUDSRV_IRX, id);
}

void sceSifRpcLoop(SifRpcDataQueue_t* queue)
{
    (void)queue;
}

SifRpcServerData_t* sceSifRemoveRpc(SifRpcServerData_t* server, SifRpcDataQueue_t* queue)
{
    (void)queue;
    ++unregisters;

    return fault == FAIL_UNREGISTER ? NULL : server;
}

SifRpcDataQueue_t* sceSifRemoveRpcQueue(SifRpcDataQueue_t* queue)
{
    ++queue_removals;

    return fault == FAIL_QUEUE ? NULL : queue;
}

void setUp(void)
{
    fault = FAIL_NONE;

    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_FALSE(live_thread);
    TEST_ASSERT_FALSE(live_sema);

    quit_calls = wait_calls = terminate_calls = thread_deletes = sema_deletes = 0;
    creates = unregisters = queue_removals = 0;
}

void tearDown(void)
{
    fault = FAIL_NONE;

    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_FALSE(live_thread);
    TEST_ASSERT_FALSE(live_sema);
}

static void shutdown_is_idempotent_and_reopens(void)
{
    TEST_ASSERT_EQUAL_INT(0, audsrv_init());
    TEST_ASSERT_EQUAL_INT(0, audsrv_init());
    TEST_ASSERT_EQUAL_INT(1, creates);
    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_EQUAL_INT(1, quit_calls);
    TEST_ASSERT_EQUAL_INT(1, unregisters);
    TEST_ASSERT_EQUAL_INT(1, queue_removals);
    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_EQUAL_INT(1, quit_calls);
    TEST_ASSERT_EQUAL_INT(0, audsrv_init());
    TEST_ASSERT_EQUAL_INT(2, creates);
}

static void failures_retain_unfinished_resources(void)
{
    const int failures[] = { FAIL_WAIT, FAIL_RPC, FAIL_REPLY, FAIL_TERMINATE, FAIL_UNREGISTER, FAIL_QUEUE, FAIL_THREAD_DELETE, FAIL_SEMA_DELETE };
    const int errors[]   = { -72, -76, 7, -73, AUDSRV_ERR_RPC_FAILED, AUDSRV_ERR_RPC_FAILED, -74, -75 };

    for (unsigned i = 0; i < sizeof(failures) / sizeof(*failures); ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, audsrv_init());

        fault = failures[i];

        TEST_ASSERT_EQUAL_INT(errors[i], audsrv_quit());
        TEST_ASSERT_EQUAL_INT(errors[i], audsrv_get_error());
        TEST_ASSERT_TRUE(live_sema);

        int allocated = creates;

        TEST_ASSERT_NOT_EQUAL(0, audsrv_init());
        TEST_ASSERT_EQUAL_INT(allocated, creates);
        TEST_ASSERT_EQUAL_INT(errors[i], audsrv_quit());

        int rpc_attempts = quit_calls;
        int stopped      = terminate_calls;
        int waits        = wait_calls;
        int removed      = unregisters;
        int queues       = queue_removals;
        fault            = FAIL_NONE;

        TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
        TEST_ASSERT_FALSE(live_thread);
        TEST_ASSERT_FALSE(live_sema);

        if (failures[i] > FAIL_REPLY)
        {
            TEST_ASSERT_EQUAL_INT(rpc_attempts, quit_calls);
        }

        if (failures[i] > FAIL_TERMINATE)
        {
            TEST_ASSERT_EQUAL_INT(stopped, terminate_calls);
        }

        if (failures[i] > FAIL_WAIT)
        {
            TEST_ASSERT_EQUAL_INT(waits, wait_calls);
        }

        if (failures[i] > FAIL_UNREGISTER)
        {
            TEST_ASSERT_EQUAL_INT(removed, unregisters);
        }

        if (failures[i] > FAIL_QUEUE)
        {
            TEST_ASSERT_EQUAL_INT(queues, queue_removals);
        }
    }
}

static void failed_thread_startup_can_be_closed(void)
{
    const int failures[] = { FAIL_THREAD_CREATE, FAIL_THREAD_START };

    for (unsigned i = 0; i < sizeof(failures) / sizeof(*failures); ++i)
    {
        fault = failures[i];

        TEST_ASSERT_NOT_EQUAL(0, audsrv_init());
        TEST_ASSERT_TRUE(live_sema);
        TEST_ASSERT_NOT_EQUAL(0, audsrv_init());

        fault = FAIL_NONE;

        TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
        TEST_ASSERT_EQUAL_INT(0, quit_calls);
        TEST_ASSERT_EQUAL_INT(0, terminate_calls);
    }
}

static void reboot_retains_ee_handles_without_calling_old_iop(void)
{
    TEST_ASSERT_EQUAL_INT(0, audsrv_init());

    iop_rebooted = 1;

    TEST_ASSERT_NOT_EQUAL(0, audsrv_init());
    TEST_ASSERT_TRUE(live_thread);
    TEST_ASSERT_TRUE(live_sema);
    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_EQUAL_INT(0, quit_calls);
    TEST_ASSERT_EQUAL_INT(0, audsrv_init());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(shutdown_is_idempotent_and_reopens);
    RUN_TEST(failures_retain_unfinished_resources);
    RUN_TEST(failed_thread_startup_can_be_closed);
    RUN_TEST(reboot_retains_ee_handles_without_calling_old_iop);

    return UNITY_END();
}
