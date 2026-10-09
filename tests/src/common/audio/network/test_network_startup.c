#include "audio/network/network.h"
#include "audio/network/ariacast/server.h"
#include "audio/network/ariacast/playback.h"
#include "audio/artwork/receiver.h"
#include "platform/network/runtime.h"
#include "platform/network/socket_mode.h"
#include "platform/thread/worker.h"
#include "platform/time/clock.h"
#include "platform/time/sleep.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>

static int      start_ok, close_ok, network_close_ok;
static int      startup_result;
static unsigned starts, references, mailbox_reads;

int platform_worker_open(Ps2Worker* worker, const Ps2WorkerConfig* config, Ps2WorkerError* error)
{
    (void)config;
    ++starts;

    worker->lock    = 1;
    worker->running = start_ok;
    *error          = (Ps2WorkerError){ "THREAD START", -42 };

    return start_ok ? 0 : -1;
}

int platform_worker_close(Ps2Worker* worker)
{
    if (worker->lock >= 0 && !close_ok)
    {
        return -1;
    }

    *worker = (Ps2Worker)PS2_WORKER_INITIALIZER;

    return 0;
}

void platform_worker_lock(const Ps2Worker* worker)
{
    (void)worker;
    ++mailbox_reads;
}

void platform_worker_unlock(const Ps2Worker* worker)
{
    (void)worker;
}

void platform_worker_finish(Ps2Worker* worker)
{
    worker->running = 0;
}

uint32_t platform_millis(void)
{
    return 0;
}

void platform_sleep_us(uint32_t microseconds)
{
    (void)microseconds;
}

int platform_network_startup(char* error, size_t capacity)
{
    if (startup_result != 0)
    {
        snprintf(error, capacity, "RESTART CONSOLE TO LOAD NETWORK MODULE");
        return startup_result;
    }

    error[0] = 0;

    ++references;

    return 0;
}

int platform_network_close(void)
{
    if (!network_close_ok)
    {
        return -1;
    }

    if (references)
    {
        --references;
    }

    return 0;
}

void platform_network_address(char* address, size_t capacity)
{
    (void)capacity;
    strcpy(address, "192.168.1.49");
}

int platform_socket_nonblocking(int fd)
{
    (void)fd;

    return 0;
}

int output_initialize(const OutputRuntime* runtime)
{
    (void)runtime;

    return 0;
}

int output_selected(OutputOwner owner)
{
    (void)owner;

    return 0;
}

int aria_server_open(AriaServer* server, int (*nonblocking)(int fd))
{
    (void)nonblocking;
    memset(server, 0, sizeof(*server));

    return 0;
}

void aria_server_close(AriaServer* server)
{
    (void)server;
}

void aria_server_step(AriaServer* server, uint32_t now, const char* address, int permitted)
{
    (void)server;
    (void)now;
    (void)address;
    (void)permitted;
}

int aria_server_active(const AriaServer* server, uint32_t now)
{
    (void)server;
    (void)now;

    return 0;
}

void aria_playback_step(AriaPlayback* playback, AriaStream* stream, AriaDiagnostics* diagnostics, const OutputRuntime* runtime, uint32_t now, void (*capture)(void* context, const uint8_t* pcm, unsigned frames), void* context)
{
    (void)playback;
    (void)stream;
    (void)diagnostics;
    (void)runtime;
    (void)now;
    (void)capture;
    (void)context;
}

int aria_playback_stop(AriaPlayback* playback)
{
    (void)playback;

    return 0;
}

void artwork_receiver_step(ArtworkReceiver* receiver, const TrackMetadata* metadata, ArtworkIdentity identity, uint32_t now, int (*nonblocking)(int), unsigned read_limit)
{
    (void)receiver;
    (void)metadata;
    (void)identity;
    (void)now;
    (void)nonblocking;
    (void)read_limit;
}

int artwork_receiver_take(ArtworkReceiver* receiver, ArtworkBlob* result)
{
    (void)receiver;
    (void)result;

    return 1;
}

void artwork_receiver_close(ArtworkReceiver* receiver)
{
    (void)receiver;
}

void setUp(void)
{
    start_ok = close_ok = network_close_ok = 1;
    startup_result                         = 0;

    TEST_ASSERT_EQUAL_INT(0, network_close());

    starts = references = mailbox_reads = 0;
}

void tearDown(void)
{
    close_ok = network_close_ok = 1;

    TEST_ASSERT_EQUAL_INT(0, network_close());
    TEST_ASSERT_EQUAL_UINT(0, references);
}

static void failed_start_with_retained_lock_preserves_error_until_reopen(void)
{
    start_ok = close_ok = 0;

    TEST_ASSERT_TRUE(!(network_open() == 0));
    TEST_ASSERT_EQUAL_UINT(1, references);
    TEST_ASSERT_EQUAL_STRING("THREAD START -42", network_error());

    unsigned reads = mailbox_reads;

    for (unsigned i = 0; i < 3; ++i)
    {
        unsigned      generation = 99;
        Audio         audio;
        TrackMetadata metadata;

        memset(&audio, 0xff, sizeof(audio));
        memset(&metadata, 0xff, sizeof(metadata));
        TEST_ASSERT_EQUAL_INT(NETWORK_ERROR_UNAVAILABLE, network_poll(&generation));
        TEST_ASSERT_EQUAL_UINT(0, generation);
        TEST_ASSERT_EQUAL_INT(NETWORK_ERROR_UNAVAILABLE, network_copy_snapshot(&generation, &audio, &metadata));
        TEST_ASSERT_EQUAL_UINT(0, generation);
        TEST_ASSERT_FALSE(audio.active);
        TEST_ASSERT_EQUAL_STRING("", metadata.title);
        TEST_ASSERT_EQUAL_STRING("THREAD START -42", network_error());
        TEST_ASSERT_EQUAL_UINT(reads, mailbox_reads);
        TEST_ASSERT_EQUAL_INT(NETWORK_ERROR_WORKER_CLOSE, network_close());
        TEST_ASSERT_TRUE(!(network_open() == 0));
        TEST_ASSERT_EQUAL_UINT(1, starts);
        TEST_ASSERT_EQUAL_UINT(1, references);
    }

    close_ok = start_ok = 1;

    TEST_ASSERT_EQUAL_INT(0, network_close());
    TEST_ASSERT_EQUAL_STRING("THREAD START -42", network_error());
    TEST_ASSERT_EQUAL_UINT(0, references);
    TEST_ASSERT_TRUE(network_open() == 0);
    TEST_ASSERT_EQUAL_UINT(2, starts);
    TEST_ASSERT_EQUAL_STRING("", network_error());

    unsigned generation;

    TEST_ASSERT_EQUAL_INT(0, network_poll(&generation));
}

static void socket_close_failure_retains_ownership(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);

    network_close_ok = 0;

    TEST_ASSERT_EQUAL_INT(NETWORK_ERROR_PLATFORM_CLOSE, network_close());
    TEST_ASSERT_EQUAL_UINT(1, references);
    TEST_ASSERT_TRUE(!(network_open() == 0));
    TEST_ASSERT_EQUAL_UINT(1, starts);
    TEST_ASSERT_EQUAL_UINT(1, references);

    network_close_ok = 1;

    TEST_ASSERT_EQUAL_INT(0, network_close());
    TEST_ASSERT_EQUAL_UINT(0, references);
    TEST_ASSERT_TRUE(network_open() == 0);
    TEST_ASSERT_EQUAL_UINT(2, starts);
}

static void restart_required_is_propagated_without_starting_worker(void)
{
    startup_result = PLATFORM_NETWORK_START_RESTART_REQUIRED;

    TEST_ASSERT_EQUAL_INT(NETWORK_START_RESTART_REQUIRED, network_open());
    TEST_ASSERT_EQUAL_STRING("RESTART CONSOLE TO LOAD NETWORK MODULE", network_error());
    TEST_ASSERT_EQUAL_UINT(0, starts);
    TEST_ASSERT_EQUAL_UINT(0, references);

    network_close_ok = 0;

    TEST_ASSERT_EQUAL_INT(NETWORK_ERROR_PLATFORM_CLOSE, network_close());
    TEST_ASSERT_EQUAL_STRING("RESTART CONSOLE TO LOAD NETWORK MODULE", network_error());

    network_close_ok = 1;

    TEST_ASSERT_EQUAL_INT(0, network_close());
    TEST_ASSERT_EQUAL_STRING("RESTART CONSOLE TO LOAD NETWORK MODULE", network_error());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(failed_start_with_retained_lock_preserves_error_until_reopen);
    RUN_TEST(socket_close_failure_retains_ownership);
    RUN_TEST(restart_required_is_propagated_without_starting_worker);

    return UNITY_END();
}
