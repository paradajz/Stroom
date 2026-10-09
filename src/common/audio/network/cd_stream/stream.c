#include "audio/network/cd_stream/stream.h"
#include "audio/network/cd_stream/server.h"
#include "audio/cd/cd_pcm.h"
#include "audio/cd/cd_runtime.h"
#include "platform/network/runtime.h"
#include "platform/network/socket_mode.h"
#include "platform/thread/worker.h"
#include "platform/thread/scheduler.h"
#include "platform/time/clock.h"
#include "platform/time/sleep.h"
#include "util/diagnostics.h"
#include <string.h>

#define STREAM_POLL_US            10000
#define STREAM_RETRY_MS           1000u
#define STREAM_ADDRESS_REFRESH_MS 2000u
#define STREAM_STACK_BYTES        16384
#define STREAM_SOURCE_GAP_MS      40u

_Static_assert(CD_OUTPUT_BYTES == CD_STREAM_MAX_PAYLOAD_BYTES, "CD network blocks must match resampler output");

static Ps2Worker      background = PS2_WORKER_INITIALIZER;
static unsigned char  stack[STREAM_STACK_BYTES] __attribute__((aligned(16)));
static CdStreamServer server;
static uint8_t        blocks[CD_STREAM_BLOCKS][CD_STREAM_MAX_PAYLOAD_BYTES];
static unsigned       read_index, count, generation, observed_generation;
static unsigned       overflows, observed_overflows;
static CdStreamSource source;
static uint32_t       source_at, drive_wait_at, output_busy_at;
static unsigned       previous_activity;
static int            finished, platform_open, started, closing;
static volatile int   accepting;
static unsigned       clients;
static int            failed, restart_required;
static uint32_t       retry_at;

static void worker(void* argument)
{
    (void)argument;

    char     address[16]     = { 0 };
    uint32_t address_checked = platform_millis() - STREAM_ADDRESS_REFRESH_MS;
    uint8_t  block[CD_STREAM_MAX_PAYLOAD_BYTES];

    while (background.running)
    {
        for (unsigned budget = 0; budget < CD_STREAM_BLOCKS; ++budget)
        {
            platform_worker_lock(&background);

            unsigned current           = generation;
            unsigned current_overflows = overflows;
            int      available         = count != 0;
            int      end               = finished && !count;

            if (available)
            {
                memcpy(block, blocks[read_index], sizeof(block));

                read_index = (read_index + 1) % CD_STREAM_BLOCKS;

                --count;
            }

            platform_worker_unlock(&background);

            if (current != observed_generation)
            {
                cd_stream_server_reset(&server);

                observed_generation = current;

                if (current_overflows != observed_overflows)
                {
                    cd_stream_server_overflows(&server, current_overflows);

                    observed_overflows = current_overflows;
                }
            }

            if (available)
            {
                cd_stream_server_push(&server, block);
            }
            else
            {
                if (end)
                {
                    cd_stream_server_end(&server);
                }

                break;
            }
        }

        uint32_t now = platform_millis();

        if ((uint32_t)(now - address_checked) >= STREAM_ADDRESS_REFRESH_MS)
        {
            platform_network_address(address, sizeof(address));

            address_checked = now;
        }

        platform_worker_lock(&background);

        server.source = source;

        platform_worker_unlock(&background);

        int result = cd_stream_server_step(&server, now, address);

        platform_worker_lock(&background);

        clients = cd_stream_server_clients(&server);
        failed  = result != 0;

        platform_worker_unlock(&background);

        if (result != 0)
        {
            accepting = 0;

            break;
        }

        platform_sleep_us(STREAM_POLL_US);
    }

    cd_stream_server_close(&server);
    platform_worker_finish(&background);
}

void cd_stream_push(const uint8_t* samples, unsigned frames)
{
    if (!accepting || frames != CD_OUTPUT_FRAMES)
    {
        return;
    }

    platform_worker_lock(&background);

    uint32_t now = platform_millis();

    if (source.blocks)
    {
        unsigned gap = now - source_at;

        if (gap > source.max_gap_ms)
        {
            source.max_gap_ms = gap;
        }

        source.gaps += gap > STREAM_SOURCE_GAP_MS;
    }

    source_at = now;

    ++source.blocks;

    if (count == CD_STREAM_BLOCKS)
    {
        /* A stalled sender cancels the old session instead of delaying CD. */
        ++generation;
        ++overflows;

        count      = 0;
        read_index = 0;
    }

    memcpy(blocks[(read_index + count) % CD_STREAM_BLOCKS], samples, CD_STREAM_MAX_PAYLOAD_BYTES);
    ++count;

    finished = 0;

    platform_worker_unlock(&background);
}

void cd_stream_activity(unsigned activity)
{
    if (!accepting)
    {
        return;
    }

    platform_worker_lock(&background);

    uint32_t       now       = platform_millis();
    const unsigned flags[]   = { CD_ACTIVITY_DRIVE_WAIT, CD_ACTIVITY_OUTPUT_BUSY };
    uint32_t*      since[]   = { &drive_wait_at, &output_busy_at };
    unsigned*      maximum[] = { &source.max_drive_wait_ms, &source.max_output_busy_ms };

    for (unsigned i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i)
    {
        if ((activity | previous_activity) & flags[i])
        {
            if (!(previous_activity & flags[i]))
            {
                *since[i] = now;
            }

            unsigned elapsed = now - *since[i];

            if (elapsed > *maximum[i])
            {
                *maximum[i] = elapsed;
            }
        }
    }

    previous_activity = activity;
    source.drive_waits += !!(activity & CD_ACTIVITY_DRIVE_WAIT);
    source.output_busy += !!(activity & CD_ACTIVITY_OUTPUT_BUSY);
    source.silence_blocks += !!(activity & CD_ACTIVITY_SILENCE);

    platform_worker_unlock(&background);
}

void cd_stream_reset(void)
{
    if (!accepting)
    {
        return;
    }

    platform_worker_lock(&background);
    ++generation;

    count             = 0;
    read_index        = 0;
    finished          = 0;
    source            = (CdStreamSource){ 0 };
    previous_activity = 0;

    platform_worker_unlock(&background);
}

void cd_stream_end(void)
{
    if (!accepting)
    {
        return;
    }

    platform_worker_lock(&background);

    finished = 1;

    platform_worker_unlock(&background);
}

int cd_stream_close(void)
{
    accepting  = 0;
    closing    = 1;
    int result = platform_worker_close(&background);

    if (result != 0)
    {
        return result < 0 ? CD_STREAM_ERROR_CLOSE : result;
    }

    if (started)
    {
        cd_stream_server_close(&server);

        started = 0;
    }

    if (platform_open)
    {
        if (platform_network_close() != 0)
        {
            return CD_STREAM_ERROR_CLOSE;
        }

        platform_open = 0;
    }

    closing = 0;
    clients = 0;

    return 0;
}

CdStreamStatus cd_stream_update(int enabled)
{
    uint32_t now = platform_millis();

    if (!enabled || closing || (started && !accepting))
    {
        if (cd_stream_close() != 0)
        {
            return (CdStreamStatus){ .failed = 1 };
        }
    }

    if (!enabled)
    {
        failed = 0;

        return (CdStreamStatus){ 0 };
    }

    if (accepting)
    {
        platform_worker_lock(&background);

        CdStreamStatus status = { clients, failed };

        platform_worker_unlock(&background);
        return status;
    }

    if (restart_required || (failed && (uint32_t)(now - retry_at) < STREAM_RETRY_MS))
    {
        return (CdStreamStatus){ .failed = 1 };
    }

    retry_at = now;
    char error[128];

    /* Failed startup may still require releasing partially acquired resources. */
    platform_open      = 1;
    int startup_result = platform_network_startup(error, sizeof(error));

    if (startup_result != 0)
    {
        restart_required = startup_result == PLATFORM_NETWORK_START_RESTART_REQUIRED;

        STROOM_LOG("CD stream network startup failed: %s", error);

        closing = 1;
        failed  = 1;

        return (CdStreamStatus){ .failed = 1 };
    }

    started = 1;

    if (cd_stream_server_open(&server, platform_socket_nonblocking) != 0)
    {
        STROOM_LOG("CD stream socket startup failed");

        closing = 1;
        failed  = 1;

        return (CdStreamStatus){ .failed = 1 };
    }

    clients                      = 0;
    observed_generation          = 0;
    generation                   = 0;
    count                        = 0;
    read_index                   = 0;
    observed_overflows           = 0;
    overflows                    = 0;
    source                       = (CdStreamSource){ 0 };
    previous_activity            = 0;
    failed                       = 0;
    finished                     = 0;
    const Ps2WorkerConfig config = { .entry = worker, .stack = stack, .stack_bytes = sizeof(stack), .priority = PS2_IO_WORKER_PRIORITY };

    if (platform_worker_open(&background, &config, NULL) != 0)
    {
        closing = 1;
        failed  = 1;

        return (CdStreamStatus){ .failed = 1 };
    }

    platform_worker_lock(&background);

    accepting = !failed;

    platform_worker_unlock(&background);

    return (CdStreamStatus){ .failed = failed };
}
