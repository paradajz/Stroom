#include "platform/cd/drive.h"
#include "platform/iop/modules.h"
#include "platform/time/clock.h"
#include <libcdvd.h>
#include "unity.h"
#include "support/process.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int          busy;
static int          completion_error;
static int          toc_result;
static int          aborts;
static int          reads;
static int          probes;
static int          complete_on_cancel;
static uint32_t     now;
static uint32_t     submission_delay;
static volatile int running = 1;

static char                    startup_error[128];
static void                    stage(void* context, const char* message);
static const Ps2CdDriveRuntime runtime = { .running = &running, .stage = stage, .error = startup_error, .capacity = sizeof(startup_error) };

int sceCdInit(int mode)
{
    (void)mode;

    return 1;
}

int sceCdTrayReq(int mode, u32* changed)
{
    (void)mode;

    *changed = 0;

    return 1;
}

int sceCdGetDiskType(void)
{
    return SCECdCDDA;
}

int sceCdGetToc(void* bytes)
{
    ((uint8_t*)bytes)[0] = 0x5a;

    return toc_result;
}

int sceCdGetError(void)
{
    return completion_error;
}

int sceCdBreak(void)
{
    ++aborts;

    if (complete_on_cancel)
    {
        busy = 0;
    }

    return 1;
}

int sceCdSync(int mode)
{
    TEST_ASSERT_TRUE_MESSAGE(mode == 1, "mode == 1"); /* No unbounded blocking sync is allowed. */
    ++probes;

    return busy;
}

int sceCdReadCDDA(int start, int count, void* bytes, sceCdRMode* mode)
{
    TEST_ASSERT_TRUE_MESSAGE(start >= 0 && count > 0 && bytes && mode->speed == SCECdSpinX2, "start >= 0 && count > 0 && bytes && mode->speed == SCECdSpinX2");
    ++reads;

    now += submission_delay;
    busy = 1;

    return 1;
}

int platform_iop_probe(unsigned id)
{
    (void)id;

    return 1;
}

int platform_iop_module(const Ps2IopModule* module, char* error, size_t capacity)
{
    (void)module;
    (void)error;
    (void)capacity;

    return 0;
}

uint32_t platform_millis(void)
{
    return now;
}

/**
 * @brief Advance the cancellation clock.
 * @param delay Microseconds.
 * @return Zero on success.
 */
int usleep(useconds_t delay)
{
    now += delay / 1000;

    return 0;
}

/**
 * @brief Ignore progress in this fixture.
 * @param context Caller context.
 * @param message Text.
 */
static void stage(void* context, const char* message)
{
    (void)context;
    (void)message;
}

/** @brief Leave track validation to audio and preserve drive failure details. */
static void raw_track_table(void)
{
    Ps2CdToc toc;
    toc_result       = 1;
    completion_error = SCECdErNO;

    TEST_ASSERT_TRUE(platform_cd_drive_toc(&toc) == 0);
    TEST_ASSERT_EQUAL_UINT8(0x5a, toc.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0, toc.data[1]);
    TEST_ASSERT_EQUAL_UINT(2064, toc.size);

    completion_error = 1;

    TEST_ASSERT_TRUE(!(platform_cd_drive_toc(&toc) == 0));
    TEST_ASSERT_EQUAL_INT(1, toc.error);

    completion_error = SCECdErNO;
    toc_result       = 0;

    TEST_ASSERT_TRUE(!(platform_cd_drive_toc(&toc) == 0));
    TEST_ASSERT_NOT_NULL(toc.data);
}

/**
 * @brief Start a pending read.
 * @param d Drive state.
 * @param start Timestamp.
 */
static void pending_read(Ps2CdDrive* d, uint32_t start)
{
    busy = aborts = reads = probes = complete_on_cancel = 0;
    completion_error                                    = SCECdErNO;
    now                                                 = start;
    submission_delay                                    = 0;

    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_open(d, &runtime) == 0, "platform_cd_drive_open(d, &runtime)");
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_prefetch(d, 100) == 0, "platform_cd_drive_prefetch(d, 100)");
    TEST_ASSERT_TRUE_MESSAGE(d->pending == 0 && reads == 1, "d->pending == 0 && reads == 1");
}

/**
 * @brief Verify normal reads, cancellation completion, and clock rollover.
 */
static void recoverable_reads(void)
{
    Ps2CdDrive d;

    pending_read(&d, 0);

    busy = 0;

    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_poll(&d, 100) == 0 && platform_cd_drive_sector(&d), "platform_cd_drive_poll(&d, 100) == 1 && platform_cd_drive_sector(&d)");
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_reset(&d, 50) == 0 && d.cursor == 50, "platform_cd_drive_reset(&d, 50) && d.cursor == 50");
    pending_read(&d, UINT32_MAX - 2500);

    uint32_t start = now;

    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_poll(&d, start + 5000) == 1 && aborts == 0, "platform_cd_drive_poll(&d, start + 5000) == 1 && aborts == 0");
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_poll(&d, start + 5001) == 1 && aborts == 1, "platform_cd_drive_poll(&d, start + 5001) == 1 && aborts == 1");
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_poll(&d, start + 5500) == 1 && aborts == 1, "platform_cd_drive_poll(&d, start + 5500) == 1 && aborts == 1");

    busy = 0;

    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_poll(&d, start + 6000) < 0, "platform_cd_drive_poll(&d, start + 6000) == 0"); /* Timed-out data is discarded even if error is zero. */
    TEST_ASSERT_TRUE_MESSAGE(!platform_cd_drive_failed(&d) && platform_cd_drive_idle(&d) && !platform_cd_drive_sector(&d), "!platform_cd_drive_failed(&d) && platform_cd_drive_idle(&d) && !platform_cd_drive_sector(&d)");
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_reset(&d, 20) == 0, "platform_cd_drive_reset(&d, 20)");

    now = start + 6001;

    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_prefetch(&d, 100) == 0 && reads == 2, "platform_cd_drive_prefetch(&d, 100) && reads == 2");

    complete_on_cancel = 1;

    platform_cd_drive_close(&d);
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_idle(&d) && aborts == 2, "platform_cd_drive_idle(&d) && aborts == 2");
}

/**
 * @brief Verify delays before and during submission do not consume a new read's deadline.
 */
static void fresh_read_deadline(void)
{
    Ps2CdDrive d;

    pending_read(&d, 0);

    busy = 0;

    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_poll(&d, now));
    TEST_ASSERT_TRUE(platform_cd_drive_reset(&d, 0) == 0);

    now              = 6000;
    submission_delay = 100;

    TEST_ASSERT_TRUE(platform_cd_drive_prefetch(&d, 100) == 0);
    TEST_ASSERT_EQUAL_UINT32(6100, d.read_at);
    TEST_ASSERT_EQUAL_INT(1, platform_cd_drive_poll(&d, now + 1));
    TEST_ASSERT_EQUAL_INT(0, aborts);
    TEST_ASSERT_EQUAL_INT(1, platform_cd_drive_poll(&d, now + 5000));
    TEST_ASSERT_EQUAL_INT(0, aborts);
    TEST_ASSERT_EQUAL_INT(1, platform_cd_drive_poll(&d, now + 5001));
    TEST_ASSERT_EQUAL_INT(1, aborts);

    busy = 0;

    platform_cd_drive_close(&d);
}

/**
 * @brief Verify a fatal read retains its DMA bank and cannot restart.
 */
static void fatal_read(void)
{
    Ps2CdDrive d;

    pending_read(&d, 0);
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_poll(&d, 5001) == 1 && aborts == 1, "platform_cd_drive_poll(&d, 5001) == 1 && aborts == 1");
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_poll(&d, 6000) == 1 && !platform_cd_drive_failed(&d), "platform_cd_drive_poll(&d, 6000) == 1 && !platform_cd_drive_failed(&d)");
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_poll(&d, 6001) == PS2_CD_DRIVE_ERROR_RESTART_REQUIRED && platform_cd_drive_failed(&d), "platform_cd_drive_poll(&d, 6001) == PS2_CD_DRIVE_ERROR_RESTART_REQUIRED && platform_cd_drive_failed(&d)");

    Ps2CdDrive saved   = d;
    int        checked = probes;

    busy = 0; /* Even a late completion cannot silently re-enable quarantined buffers. */

    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_poll(&d, 9000) == PS2_CD_DRIVE_ERROR_RESTART_REQUIRED, "platform_cd_drive_poll(&d, 9000) == PS2_CD_DRIVE_ERROR_RESTART_REQUIRED");
    TEST_ASSERT_TRUE_MESSAGE(!platform_cd_drive_idle(&d) && !platform_cd_drive_sector(&d) && platform_cd_drive_waiting(&d), "!platform_cd_drive_idle(&d) && !platform_cd_drive_sector(&d) && platform_cd_drive_waiting(&d)");
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_reset(&d, 75) != 0, "!platform_cd_drive_reset(&d, 75)");
    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_prefetch(&d, 100) != 0, "!platform_cd_drive_prefetch(&d, 100)");
    platform_cd_drive_consume(&d);
    platform_cd_drive_close(&d);
    TEST_ASSERT_TRUE_MESSAGE(!memcmp(&d, &saved, sizeof(d)) && aborts == 1 && reads == 1 && probes == checked, "!memcmp(&d, &saved, sizeof(d)) && aborts == 1 && reads == 1 && probes == checked");

    Ps2CdDrive reopened;

    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_open(&reopened, &runtime) != 0 && platform_cd_drive_failed(&reopened), "!platform_cd_drive_open(&reopened, &runtime) && platform_cd_drive_failed(&reopened)");
    TEST_ASSERT_TRUE_MESSAGE(strstr(startup_error, "RESTART REQUIRED"), "strstr(startup_error, \"RESTART REQUIRED\")");
}

/**
 * @brief Verify seek cancellation has the same bounded failure path.
 */
static void fatal_seek(void)
{
    Ps2CdDrive d;

    pending_read(&d, 0);

    int bank = d.pending;

    TEST_ASSERT_TRUE_MESSAGE(platform_cd_drive_reset(&d, 75) != 0, "!platform_cd_drive_reset(&d, 75)");
    TEST_ASSERT_TRUE_MESSAGE(now == 1000 && aborts == 1 && d.pending == bank && d.cursor == 16, "now == 1000 && aborts == 1 && d.pending == bank && d.cursor == 16");
    platform_cd_drive_close(&d);
    TEST_ASSERT_TRUE_MESSAGE(now == 1000 && aborts == 1, "now == 1000 && aborts == 1");
}

/**
 * @brief Check drive deadlines with fresh process-global DMA ownership.
 */
static void recoverable_case(void)
{
    test_process_run(recoverable_reads);
}

/**
 * @brief Isolate permanent DMA quarantine from subsequent cases.
 */
static void fatal_read_case(void)
{
    test_process_run(fatal_read);
}

/**
 * @brief Isolate fatal seek cancellation from subsequent cases.
 */
static void fatal_seek_case(void)
{
    test_process_run(fatal_seek);
}

/**
 * @brief Fixtures are initialized by each scenario.
 */
void setUp(void)
{}

/**
 * @brief This suite owns no external resources.
 */
void tearDown(void)
{}

/**
 * @brief Run the regression scenario.
 * @return Number of failed Unity cases.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(raw_track_table);
    RUN_TEST(fresh_read_deadline);
    RUN_TEST(recoverable_case);
    RUN_TEST(fatal_read_case);
    RUN_TEST(fatal_seek_case);

    return UNITY_END();
}
