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
static int          last_start, last_count, reject_read;
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

    last_start = start;
    last_count = count;

    if (reject_read)
    {
        return 0;
    }

    for (int i = 0; i < count; ++i)
    {
        uint32_t value = (uint32_t)(start + i);

        memcpy((uint8_t*)bytes + i * CD_SECTOR_BYTES, &value, sizeof(value));
        memcpy((uint8_t*)bytes + (i + 1) * CD_SECTOR_BYTES - sizeof(value), &value, sizeof(value));
    }

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
    complete_on_cancel = 0;
    probes             = 0;
    reads              = 0;
    aborts             = 0;
    busy               = 0;
    completion_error   = SCECdErNO;
    now                = start;
    submission_delay   = 0;
    reject_read        = 0;

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

static void complete_read(Ps2CdDrive* d)
{
    busy = 0;

    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_poll(d, now));
}

static void fill_reserve(Ps2CdDrive* d, int end)
{
    while (platform_cd_drive_buffered(d) < PS2_CD_RESERVE_SECTORS && d->cursor < end)
    {
        TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(d, end));
        complete_read(d);
    }
}

static void consume_sector(Ps2CdDrive* d, unsigned expected)
{
    const uint8_t* data = platform_cd_drive_sector(d);

    TEST_ASSERT_NOT_NULL(data);

    uint32_t first, last;

    memcpy(&first, data, sizeof(first));
    memcpy(&last, data + CD_SECTOR_BYTES - sizeof(last), sizeof(last));
    TEST_ASSERT_EQUAL_UINT32(expected, first);
    TEST_ASSERT_EQUAL_UINT32(expected, last);
    platform_cd_drive_consume(d);
}

/** A filled reserve plays for its configured duration without new read completions. */
static void reserve_and_wraparound(void)
{
    Ps2CdDrive d;
    const int  end = 4 * PS2_CD_RESERVE_SECTORS;

    pending_read(&d, 0);
    TEST_ASSERT_NULL(platform_cd_drive_sector(&d));
    complete_read(&d);
    fill_reserve(&d, end);
    TEST_ASSERT_EQUAL_UINT(PS2_CD_RESERVE_SECTORS, platform_cd_drive_buffered(&d));
    TEST_ASSERT_EQUAL_INT(PS2_CD_RESERVE_SECTORS, d.cursor);
    TEST_ASSERT_EQUAL_INT((PS2_CD_RESERVE_SECTORS - 1) % 16 + 1, last_count);

    int before = reads;

    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, end));
    TEST_ASSERT_EQUAL_INT(before, reads);

    for (unsigned i = 0; i < PS2_CD_RESERVE_SECTORS; ++i)
    {
        now = (i + 1) * 1000 / CD_SECTORS_PER_SECOND;

        consume_sector(&d, i);
        TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, end));
        TEST_ASSERT_EQUAL_INT(i + 1 >= 16 ? before + 1 : before, reads);
        /* Playback drains ready sectors even when the reader cannot poll. */
    }

    TEST_ASSERT_NULL(platform_cd_drive_sector(&d));
    TEST_ASSERT_TRUE(platform_cd_drive_waiting(&d));
    TEST_ASSERT_EQUAL_UINT32(PS2_CD_RESERVE_MS, now);
    TEST_ASSERT_EQUAL_INT(0, aborts);
    complete_read(&d);

    for (unsigned i = PS2_CD_RESERVE_SECTORS; i < 3 * PS2_CD_RESERVE_SECTORS; ++i)
    {
        if (!platform_cd_drive_buffered(&d))
        {
            TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, end));
            complete_read(&d);
        }

        consume_sector(&d, i);
    }

    complete_on_cancel = 1;

    platform_cd_drive_close(&d);
}

/** A short prefill grows toward capacity without falling into tiny refill reads. */
static void partial_prefill_refills_in_whole_chunks(void)
{
    Ps2CdDrive d;
    const int  end = 3 * PS2_CD_RESERVE_SECTORS;

    pending_read(&d, 0);
    complete_read(&d);

    while (platform_cd_drive_buffered(&d) < PS2_CD_PREFILL_SECTORS)
    {
        TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, end));
        complete_read(&d);
    }

    consume_sector(&d, 0);

    unsigned next = 1;

    while (platform_cd_drive_buffered(&d) + 16 <= PS2_CD_RESERVE_SECTORS)
    {
        TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, end));
        TEST_ASSERT_EQUAL_INT(16, last_count);
        complete_read(&d);
        /* Consume at half the reading rate while the reserve grows. */

        for (unsigned i = 0; i < 8; ++i)
        {
            consume_sector(&d, next++);
        }
    }

    int before = reads;

    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, end));
    TEST_ASSERT_EQUAL_INT(before, reads);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(PS2_CD_RESERVE_SECTORS - 15, platform_cd_drive_buffered(&d));

    for (unsigned i = 0; i < 16; ++i)
    {
        consume_sector(&d, next++);
    }

    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, end));
    TEST_ASSERT_EQUAL_INT(before + 1, reads);
    TEST_ASSERT_EQUAL_INT(16, last_count);
    complete_read(&d);
    consume_sector(&d, next);
    platform_cd_drive_close(&d);
}

/** A failed chunk is retried at the same LBA without discarding the remaining reserve. */
static void retry_keeps_buffered_audio(void)
{
    Ps2CdDrive d;
    const int  end = PS2_CD_RESERVE_SECTORS + 16;

    pending_read(&d, 0);
    complete_read(&d);
    fill_reserve(&d, end);

    for (unsigned i = 0; i < 16; ++i)
    {
        consume_sector(&d, i);
    }

    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, end));
    TEST_ASSERT_EQUAL_INT(PS2_CD_RESERVE_SECTORS, last_start);

    busy             = 0;
    completion_error = 1;

    TEST_ASSERT_EQUAL_INT(PS2_CD_DRIVE_ERROR_READ, platform_cd_drive_poll(&d, now));
    TEST_ASSERT_EQUAL_INT(PS2_CD_RESERVE_SECTORS, d.cursor);
    TEST_ASSERT_EQUAL_UINT(PS2_CD_RESERVE_SECTORS - 16, platform_cd_drive_buffered(&d));

    completion_error = SCECdErNO;
    reject_read      = 1;

    TEST_ASSERT_EQUAL_INT(PS2_CD_DRIVE_ERROR_READ_START, platform_cd_drive_prefetch(&d, end));
    TEST_ASSERT_EQUAL_INT(PS2_CD_RESERVE_SECTORS, d.cursor);

    reject_read = 0;

    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, end));
    TEST_ASSERT_EQUAL_INT(PS2_CD_RESERVE_SECTORS, last_start);
    complete_read(&d);

    for (unsigned i = 16; i < PS2_CD_RESERVE_SECTORS + 16; ++i)
    {
        consume_sector(&d, i);
    }

    TEST_ASSERT_NULL(platform_cd_drive_sector(&d));
}

/** Short boundaries finish prefetching; seeks cancel DMA and expose only new-position data. */
static void short_tail_and_seek_discard(void)
{
    Ps2CdDrive d;

    pending_read(&d, 0);
    complete_read(&d);
    fill_reserve(&d, 23);
    TEST_ASSERT_EQUAL_UINT(23, platform_cd_drive_buffered(&d));
    TEST_ASSERT_EQUAL_INT(7, last_count);

    for (unsigned i = 0; i < 23; ++i)
    {
        consume_sector(&d, i);
    }

    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, 23));
    TEST_ASSERT_TRUE(platform_cd_drive_idle(&d));
    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_reset(&d, 100));
    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, 1000));
    complete_read(&d);
    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, 1000));

    complete_on_cancel = 1;

    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_reset(&d, 600));
    TEST_ASSERT_EQUAL_UINT(0, platform_cd_drive_buffered(&d));
    TEST_ASSERT_NULL(platform_cd_drive_sector(&d));
    TEST_ASSERT_EQUAL_INT(0, platform_cd_drive_prefetch(&d, 610));
    complete_read(&d);

    for (unsigned i = 600; i < 610; ++i)
    {
        consume_sector(&d, i);
    }
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
    RUN_TEST(reserve_and_wraparound);
    RUN_TEST(partial_prefill_refills_in_whole_chunks);
    RUN_TEST(retry_keeps_buffered_audio);
    RUN_TEST(short_tail_and_seek_discard);
    RUN_TEST(recoverable_case);
    RUN_TEST(fatal_read_case);
    RUN_TEST(fatal_seek_case);

    return UNITY_END();
}
