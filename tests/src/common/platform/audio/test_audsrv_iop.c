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

int sceSdInit(int mode)
{
    (void)mode;

    return 0;
}

void sceSdSetParam(int param, unsigned short value)
{
    (void)param;
    (void)value;
}

void* sceSdSetTransCallback(int channel, void* callback)
{
    (void)channel;
    (void)callback;

    return NULL;
}

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

int sceSdBlockTransStatus(int channel, int mode)
{
    (void)channel;
    (void)mode;

    return 0;
}

int CreateMutex(int state)
{
    (void)state;
    ++live_semas;

    return ++creations;
}

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

int StartThread(int thread, void* argument)
{
    if (thread == 41)
    {
        rpc_entry(argument);
        return 0;
    }

    return fault == FAIL_START ? -86 : 0;
}

int TerminateThread(int thread)
{
    TEST_ASSERT_EQUAL_INT(31, thread);
    TEST_ASSERT_TRUE(live_thread);
    ++terminations;

    return fault == FAIL_TERMINATE ? -82 : 0;
}

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

int WaitSema(int sema)
{
    (void)sema;
    ++wait_calls;

    return fault == FAIL_CALLBACK_WAIT ? -87 : 0;
}

int SignalSema(int sema)
{
    (void)sema;

    return 0;
}

int iSignalSema(int sema)
{
    (void)sema;

    return 0;
}

int audsrv_stop_cd(void)
{
    return 0;
}

int audsrv_adpcm_init(void)
{
    return 0;
}

/* The renderer code keeps these reachable through the worker entry point. */
void CpuSuspendIntr(int* state)
{
    *state = 0;
}

void CpuResumeIntr(int state)
{
    (void)state;
}

void wmemcpy(void* dest, const void* src, int numwords)
{
    (void)dest;
    (void)src;
    (void)numwords;
}

void FlushDcache(void)
{}

void CpuEnableIntr(void)
{}

int RegisterLibraryEntries(struct irx_export_table* entries)
{
    (void)entries;

    return 0;
}

int GetThreadId(void)
{
    return 41;
}

void sceSifInitRpc(int mode)
{
    (void)mode;
}

void sceSifSetRpcQueue(SifRpcDataQueue_t* queue, int thread)
{
    (void)queue;
    (void)thread;
}

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

void sceSifRpcLoop(SifRpcDataQueue_t* queue)
{
    (void)queue;
}

int sceSifBindRpc(SifRpcClientData_t* client, int id, int mode)
{
    (void)id;
    (void)mode;

    client->server = client;

    return 0;
}

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

int DelayThread(int microseconds)
{
    (void)microseconds;

    return 0;
}

/* Unused CD/ADPCM wire operations remain linked with the complete dispatcher. */
int audsrv_play_cd(int track)
{
    (void)track;

    return 0;
}

int audsrv_get_cdpos(void)
{
    return 0;
}

int audsrv_get_trackpos(void)
{
    return 0;
}

int audsrv_get_numtracks(void)
{
    return 0;
}

int audsrv_get_track_offset(int track)
{
    (void)track;

    return 0;
}

int audsrv_cd_play_sectors(int start, int end)
{
    (void)start;
    (void)end;

    return 0;
}

int audsrv_get_cd_status(void)
{
    return 0;
}

int audsrv_get_cd_type(void)
{
    return 0;
}

int audsrv_cd_pause(void)
{
    return 0;
}

int audsrv_cd_resume(void)
{
    return 0;
}

void* audsrv_load_adpcm(u32* buffer, int size, int id)
{
    (void)buffer;
    (void)size;
    (void)id;

    return NULL;
}

int audsrv_ch_play_adpcm(int ch, u32 id)
{
    (void)ch;
    (void)id;

    return 0;
}

int audsrv_is_adpcm_playing(int ch, u32 id)
{
    (void)ch;
    (void)id;

    return 0;
}

int free_sample(u32 id)
{
    (void)id;

    return 0;
}

int audsrv_adpcm_set_volume(int ch, int voll, int volr)
{
    (void)ch;
    (void)voll;
    (void)volr;

    return 0;
}

void setUp(void)
{
    fault = FAIL_NONE;

    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_FALSE(live_thread);
    TEST_ASSERT_EQUAL_INT(0, live_semas);

    dma_stops = terminations = thread_deletes = sema_deletes = wait_calls = 0;
}

void tearDown(void)
{
    fault = FAIL_NONE;

    TEST_ASSERT_EQUAL_INT(0, audsrv_quit());
    TEST_ASSERT_FALSE(live_thread);
    TEST_ASSERT_EQUAL_INT(0, live_semas);
}

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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(shutdown_is_idempotent_and_reopens);
    RUN_TEST(failures_retain_handles_and_skip_completed_steps);
    RUN_TEST(failed_start_retains_dormant_thread_for_cleanup);
    RUN_TEST(callback_cleanup_waits_and_retains_semaphore);

    return UNITY_END();
}
