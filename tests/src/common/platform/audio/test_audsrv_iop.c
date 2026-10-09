#include <audsrv.h>
#include <thbase.h>
#include <thsemap.h>
#include <libsd.h>
#include <sifrpc.h>
#include <loadcore.h>
#include <module.h>
#include "unity.h"

/* Exercise the public IOP sound lifecycle. Hardware substitutes retain the
 * handles they received, rather than inspecting SDK implementation state. */
static int fault, live_thread, live_semas, creations;
static int dma_stops, terminations, thread_deletes, sema_deletes;
static int wait_calls;
static void (*rpc_entry)(void*);
static void* (*rpc_handler)(int, void*, int);
struct irx_export_table _exp_audsrv;

enum
{
    FAIL_NONE,
    FAIL_DMA,
    FAIL_TERMINATE,
    FAIL_THREAD_DELETE,
    FAIL_TRANSFER_DELETE,
    FAIL_QUEUE_DELETE,
    FAIL_START,
    FAIL_CALLBACK_DELETE,
    FAIL_CALLBACK_WAIT
};

/**
 * @brief Substitute sceSdInit in this host fixture.
 */
int sceSdInit(int mode)
{
    (void)mode;

    return 0;
}

/**
 * @brief Substitute sceSdSetParam in this host fixture.
 */
void sceSdSetParam(int param, unsigned short value)
{
    (void)param;
    (void)value;
}

/**
 * @brief Substitute sceSdSetTransCallback in this host fixture.
 */
void* sceSdSetTransCallback(int channel, void* callback)
{
    (void)channel;
    (void)callback;

    return NULL;
}

/**
 * @brief Substitute sceSdBlockTrans in this host fixture.
 */
int sceSdBlockTrans(int channel, int mode, void* buffer, int bytes, void* argument)
{
    (void)channel;
    (void)buffer;
    (void)bytes;
    (void)argument;

    if (mode == SD_TRANS_STOP)
    {
        ++dma_stops;

        if (fault == FAIL_DMA)
        {
            return -81;
        }
    }

    return 0;
}

/**
 * @brief Substitute sceSdBlockTransStatus in this host fixture.
 */
int sceSdBlockTransStatus(int channel, int mode)
{
    (void)channel;
    (void)mode;

    return 0;
}

/**
 * @brief Substitute CreateMutex in this host fixture.
 */
int CreateMutex(int state)
{
    (void)state;
    ++live_semas;

    return ++creations;
}

/**
 * @brief Substitute CreateThread in this host fixture.
 */
int CreateThread(iop_thread_t* thread)
{
    if (thread->priority == 40)
    {
        rpc_entry = thread->thread;

        return 41;
    }

    TEST_ASSERT_FALSE(live_thread);

    live_thread = 1;

    return 31;
}

/**
 * @brief Substitute StartThread in this host fixture.
 */
int StartThread(int thread, void* argument)
{
    if (thread == 41)
    {
        rpc_entry(argument);
        return 0;
    }

    return fault == FAIL_START ? -86 : 0;
}

/**
 * @brief Substitute TerminateThread in this host fixture.
 */
int TerminateThread(int thread)
{
    TEST_ASSERT_EQUAL_INT(31, thread);
    TEST_ASSERT_TRUE(live_thread);
    ++terminations;

    return fault == FAIL_TERMINATE ? -82 : 0;
}

/**
 * @brief Substitute DeleteThread in this host fixture.
 */
int DeleteThread(int thread)
{
    TEST_ASSERT_EQUAL_INT(31, thread);
    TEST_ASSERT_TRUE(live_thread);
    ++thread_deletes;

    if (fault == FAIL_THREAD_DELETE)
    {
        return -83;
    }

    live_thread = 0;

    return 0;
}

/**
 * @brief Substitute DeleteSema in this host fixture.
 */
int DeleteSema(int sema)
{
    ++sema_deletes;

    if ((fault == FAIL_TRANSFER_DELETE && sema == creations - 1) ||
        (fault == FAIL_QUEUE_DELETE && sema == creations) ||
        (fault == FAIL_CALLBACK_DELETE && sema == creations))
    {
        return -84;
    }

    TEST_ASSERT_GREATER_THAN_INT(0, live_semas);
    --live_semas;

    return 0;
}

/**
 * @brief Substitute WaitSema in this host fixture.
 */
int WaitSema(int sema)
{
    (void)sema;
    ++wait_calls;

    return fault == FAIL_CALLBACK_WAIT ? -87 : 0;
}

/**
 * @brief Substitute SignalSema in this host fixture.
 */
int SignalSema(int sema)
{
    (void)sema;

    return 0;
}

/**
 * @brief Substitute iSignalSema in this host fixture.
 */
int iSignalSema(int sema)
{
    (void)sema;

    return 0;
}

/**
 * @brief Substitute audsrv_stop_cd in this host fixture.
 */
int audsrv_stop_cd(void)
{
    return 0;
}

/**
 * @brief Substitute audsrv_adpcm_init in this host fixture.
 */
int audsrv_adpcm_init(void)
{
    return 0;
}

/**
 * @brief Substitute CpuSuspendIntr in this host fixture.
 * The renderer code keeps these reachable through the worker entry point.
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
 * @brief Substitute wmemcpy in this host fixture.
 */
void wmemcpy(void* dest, const void* src, int numwords)
{
    (void)dest;
    (void)src;
    (void)numwords;
}

/**
 * @brief Substitute FlushDcache in this host fixture.
 */
void FlushDcache(void)
{}

/**
 * @brief Substitute CpuEnableIntr in this host fixture.
 */
void CpuEnableIntr(void)
{}

/**
 * @brief Substitute RegisterLibraryEntries in this host fixture.
 */
int RegisterLibraryEntries(struct irx_export_table* entries)
{
    (void)entries;

    return 0;
}

/**
 * @brief Substitute GetThreadId in this host fixture.
 */
int GetThreadId(void)
{
    return 41;
}

/**
 * @brief Substitute sceSifInitRpc in this host fixture.
 */
void sceSifInitRpc(int mode)
{
    (void)mode;
}

/**
 * @brief Substitute sceSifSetRpcQueue in this host fixture.
 */
void sceSifSetRpcQueue(SifRpcDataQueue_t* queue, int thread)
{
    (void)queue;
    (void)thread;
}

/**
 * @brief Substitute sceSifRegisterRpc in this host fixture.
 */
void sceSifRegisterRpc(SifRpcServerData_t* server, int id, void* (*handler)(int, void*, int), void* buffer, void* callback, void* callback_buffer, SifRpcDataQueue_t* queue)
{
    (void)server;
    (void)id;
    (void)buffer;
    (void)callback;
    (void)callback_buffer;
    (void)queue;

    rpc_handler = handler;
}

/**
 * @brief Substitute sceSifRpcLoop in this host fixture.
 */
void sceSifRpcLoop(SifRpcDataQueue_t* queue)
{
    (void)queue;
}

/**
 * @brief Substitute sceSifBindRpc in this host fixture.
 */
int sceSifBindRpc(SifRpcClientData_t* client, int id, int mode)
{
    (void)id;
    (void)mode;

    client->server = client;

    return 0;
}

/**
 * @brief Substitute sceSifCallRpc in this host fixture.
 */
int sceSifCallRpc(SifRpcClientData_t* client, int command, int mode, void* input, int input_size, void* output, int output_size, void (*callback)(void*), void* argument)
{
    (void)client;
    (void)command;
    (void)mode;
    (void)input;
    (void)input_size;
    (void)output;
    (void)output_size;

    if (callback)
    {
        callback(argument);
    }

    return 0;
}

/**
 * @brief Substitute DelayThread in this host fixture.
 */
int DelayThread(int microseconds)
{
    (void)microseconds;

    return 0;
}

/**
 * @brief Substitute audsrv_play_cd in this host fixture.
 * Unused CD/ADPCM wire operations remain linked with the complete dispatcher.
 */
int audsrv_play_cd(int track)
{
    (void)track;

    return 0;
}

/**
 * @brief Substitute audsrv_get_cdpos in this host fixture.
 */
int audsrv_get_cdpos(void)
{
    return 0;
}

/**
 * @brief Substitute audsrv_get_trackpos in this host fixture.
 */
int audsrv_get_trackpos(void)
{
    return 0;
}

/**
 * @brief Substitute audsrv_get_numtracks in this host fixture.
 */
int audsrv_get_numtracks(void)
{
    return 0;
}

/**
 * @brief Substitute audsrv_get_track_offset in this host fixture.
 */
int audsrv_get_track_offset(int track)
{
    (void)track;

    return 0;
}

/**
 * @brief Substitute audsrv_cd_play_sectors in this host fixture.
 */
int audsrv_cd_play_sectors(int start, int end)
{
    (void)start;
    (void)end;

    return 0;
}

/**
 * @brief Substitute audsrv_get_cd_status in this host fixture.
 */
int audsrv_get_cd_status(void)
{
    return 0;
}

/**
 * @brief Substitute audsrv_get_cd_type in this host fixture.
 */
int audsrv_get_cd_type(void)
{
    return 0;
}

/**
 * @brief Substitute audsrv_cd_pause in this host fixture.
 */
int audsrv_cd_pause(void)
{
    return 0;
}

/**
 * @brief Substitute audsrv_cd_resume in this host fixture.
 */
int audsrv_cd_resume(void)
{
    return 0;
}

/**
 * @brief Substitute audsrv_load_adpcm in this host fixture.
 */
void* audsrv_load_adpcm(u32* buffer, int size, int id)
{
    (void)buffer;
    (void)size;
    (void)id;

    return NULL;
}

/**
 * @brief Substitute audsrv_ch_play_adpcm in this host fixture.
 */
int audsrv_ch_play_adpcm(int ch, u32 id)
{
    (void)ch;
    (void)id;

    return 0;
}

/**
 * @brief Substitute audsrv_is_adpcm_playing in this host fixture.
 */
int audsrv_is_adpcm_playing(int ch, u32 id)
{
    (void)ch;
    (void)id;

    return 0;
}

/**
 * @brief Substitute free_sample in this host fixture.
 */
int free_sample(u32 id)
{
    (void)id;

    return 0;
}

/**
 * @brief Substitute audsrv_adpcm_set_volume in this host fixture.
 */
int audsrv_adpcm_set_volume(int ch, int voll, int volr)
{
    (void)ch;
    (void)voll;
    (void)volr;

    return 0;
}

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    fault = FAIL_NONE;

    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_FALSE(live_thread);
    TEST_ASSERT_EQUAL_INT(0, live_semas);

    wait_calls     = 0;
    sema_deletes   = 0;
    thread_deletes = 0;
    terminations   = 0;
    dma_stops      = 0;
}

/**
 * @brief Finish fixture cleanup and resource checks after each test.
 */
void tearDown(void)
{
    fault = FAIL_NONE;

    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_FALSE(live_thread);
    TEST_ASSERT_EQUAL_INT(0, live_semas);
}

/**
 * @brief Verify shutdown is idempotent and reopens.
 */
static void shutdown_is_idempotent_and_reopens(void)
{
    TEST_ASSERT_EQUAL_INT(0, audsrv_init());
    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_EQUAL_INT(1, dma_stops);
    TEST_ASSERT_EQUAL_INT(1, terminations);
    TEST_ASSERT_EQUAL_INT(1, thread_deletes);
    TEST_ASSERT_EQUAL_INT(2, sema_deletes);
    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_EQUAL_INT(2, sema_deletes);
    TEST_ASSERT_EQUAL_INT(0, audsrv_init());
    TEST_ASSERT_EQUAL_INT(2, live_semas);
}

/**
 * @brief Verify failures retain handles and skip completed steps.
 */
static void failures_retain_handles_and_skip_completed_steps(void)
{
    const int failures[] = { FAIL_DMA, FAIL_TERMINATE, FAIL_THREAD_DELETE, FAIL_TRANSFER_DELETE, FAIL_QUEUE_DELETE };

    for (unsigned i = 0; i < sizeof(failures) / sizeof(*failures); ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, audsrv_init());

        fault = failures[i];

        TEST_ASSERT_NOT_EQUAL(0, audsrv_quit());
        TEST_ASSERT_GREATER_THAN_INT(0, live_semas);

        int allocated = creations;

        TEST_ASSERT_NOT_EQUAL(0, audsrv_init());
        TEST_ASSERT_EQUAL_INT(allocated, creations);
        TEST_ASSERT_NOT_EQUAL(0, audsrv_quit());

        int stopped = dma_stops, terminated = terminations, deleted = thread_deletes;
        fault = FAIL_NONE;

        TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
        TEST_ASSERT_EQUAL_INT(0, live_semas);

        if (failures[i] > FAIL_DMA)
        {
            TEST_ASSERT_EQUAL_INT(stopped, dma_stops);
        }

        if (failures[i] > FAIL_TERMINATE)
        {
            TEST_ASSERT_EQUAL_INT(terminated, terminations);
        }

        if (failures[i] > FAIL_THREAD_DELETE)
        {
            TEST_ASSERT_EQUAL_INT(deleted, thread_deletes);
        }
    }
}

/**
 * @brief Verify failed start retains dormant thread for cleanup.
 */
static void failed_start_retains_dormant_thread_for_cleanup(void)
{
    fault = FAIL_START;

    TEST_ASSERT_EQUAL_INT(-86, audsrv_init());
    TEST_ASSERT_TRUE(live_thread);

    fault = FAIL_NONE;

    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_EQUAL_INT(0, terminations);
    TEST_ASSERT_EQUAL_INT(1, thread_deletes);
}

/**
 * @brief Verify callback cleanup waits and retains semaphore.
 */
static void callback_cleanup_waits_and_retains_semaphore(void)
{
    TEST_ASSERT_EQUAL_INT(MODULE_RESIDENT_END, audsrv_module_start(0, NULL));
    TEST_ASSERT_NOT_NULL(rpc_handler);

    int args[4] = { 0 };

    rpc_handler(AUDSRV_INIT, args, sizeof(args));
    TEST_ASSERT_EQUAL_INT(0, args[0]);
    TEST_ASSERT_EQUAL_INT(3, live_semas);

    fault = FAIL_CALLBACK_WAIT;

    TEST_ASSERT_EQUAL_INT(-87, audsrv_quit());
    TEST_ASSERT_EQUAL_INT(3, live_semas);

    fault = FAIL_CALLBACK_DELETE;

    TEST_ASSERT_EQUAL_INT(-84, audsrv_quit());

    int waits = wait_calls;

    TEST_ASSERT_EQUAL_INT(-84, audsrv_quit());
    TEST_ASSERT_EQUAL_INT(waits, wait_calls);
    TEST_ASSERT_EQUAL_INT(3, live_semas);

    fault = FAIL_NONE;

    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_EQUAL_INT(waits, wait_calls);
    TEST_ASSERT_EQUAL_INT(0, live_semas);
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(shutdown_is_idempotent_and_reopens);
    RUN_TEST(failures_retain_handles_and_skip_completed_steps);
    RUN_TEST(failed_start_retains_dormant_thread_for_cleanup);
    RUN_TEST(callback_cleanup_waits_and_retains_semaphore);

    return UNITY_END();
}
