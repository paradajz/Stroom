/* Compile the actual adapter with deterministic socket and EE thread substitutes. */
#include "audio/network/network.h"
#include "audio/network/ariacast/server.h"
#include "audio/network/ariacast/playback.h"
#include "audio/artwork/receiver.h"
#include "audio/common/pcm.h"
#include "platform/network/runtime.h"
#include "platform/network/rpc/bridge.h"
#include "platform/iop/modules.h"
#include "platform/iop/services.h"
#include "support/worker_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "unity.h"
#include <kernel.h>

static uint32_t    stop_at;
static AriaServer* mock_server;
static ArtworkBlob queued_artwork;
static void (*pcm_capture)(void*, const uint8_t*, unsigned);
static void*            capture_context;
static AriaDiagnostics* current_diagnostics;
static unsigned         address_queries;
static int              startup_failure;
static int              status_failure;
static int              semaphores[8];
static int              next_semaphore;

static const char* module_names[] = { "dev9", "Network_Manager", "SMAP_driver", "TCP/IP Stack", "STROOM_TCPIP_RPC" };
static int         resident[5];
static int         loads;
static int         load_failure;
static int         start_failure;
static int         bind_failure;
static int         interface_missing;
static int         config_failure;
static int         dhcp_unsupported;
static int         config_writes;
static int         disconnected;
static int         patches;
static t_ip_info   interface_config;

static int      artwork_requested;
static unsigned observed_generation;
static void (*playback_hook)(void);
static int      mock_bridge_compatible;
static int      mock_aria_active;
static int      mock_aria_pending;
static int      mock_ioctl_error;
static int      mock_aria_failure;
static int      mock_stall_poll;
static int      mock_listener_failure;
static int      mock_cd_selected;
static uint32_t clock_ms;

void artwork_receiver_step(ArtworkReceiver* receiver, const TrackMetadata* metadata, ArtworkIdentity identity, uint32_t now, int (*nonblocking)(int), unsigned read_limit)
{
    TEST_ASSERT_EQUAL_INT(ARTWORK_PER_TRACK, identity);
    TEST_ASSERT_EQUAL_UINT(1, read_limit);
    (void)receiver;

    artwork_requested = metadata->artwork_url[0] != 0;

    (void)now;
    (void)nonblocking;
}

int artwork_receiver_take(ArtworkReceiver* receiver, ArtworkBlob* result)
{
    (void)receiver;

    if (!queued_artwork.data)
    {
        return 1;
    }

    *result        = queued_artwork;
    queued_artwork = (ArtworkBlob){ 0 };

    return 0;
}

void artwork_receiver_close(ArtworkReceiver* receiver)
{
    (void)receiver;
}

int aria_server_open(AriaServer* server, int (*nonblocking)(int fd))
{
    (void)nonblocking;

    mock_server = server;

    memset(server, 0, sizeof(*server));

    server->audio_fd = -1;

    if (mock_aria_failure)
    {
        strcpy(server->error, "ARIA TCP 12889 SOCKET ERR 23");
        return -1;
    }

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

    if (mock_stall_poll)
    {
        mock_stall_poll   = 0;
        server->operation = "ACCEPT";
        clock_ms += 2000;

        TEST_ASSERT_EQUAL_INT(0, network_poll(&observed_generation));
        TEST_ASSERT_EQUAL_STRING("NETWORK STALLED: ACCEPT", network_error());
    }

    if (mock_listener_failure)
    {
        server->listener = -1;
        mock_aria_active = 0;

        strcpy(server->error, "ARIA LISTENER FAILED");
    }

    if (!permitted)
    {
        mock_aria_active = 0;
        server->audio_fd = -1;
    }
    else if (mock_aria_pending)
    {
        server->audio_fd = 7;
    }
}

int aria_server_active(const AriaServer* server, uint32_t now)
{
    (void)server;
    (void)now;

    return mock_aria_active;
}

void aria_playback_step(AriaPlayback* playback, AriaStream* stream, AriaDiagnostics* diagnostics, const OutputRuntime* runtime, uint32_t now, void (*capture)(void*, const uint8_t*, unsigned), void* context)
{
    pcm_capture         = capture;
    capture_context     = context;
    current_diagnostics = diagnostics;

    if (playback_hook)
    {
        playback_hook();
    }

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

int output_initialize(const OutputRuntime* runtime)
{
    (void)runtime;

    return 0;
}

int output_selected(OutputOwner owner)
{
    return owner == OUTPUT_CD && mock_cd_selected;
}

unsigned char ps2dev9_irx[1];
unsigned int  size_ps2dev9_irx = 1;
unsigned char netman_irx[1];
unsigned int  size_netman_irx = 1;
unsigned char ps2ip_netman_irx[1];
unsigned int  size_ps2ip_netman_irx = 1;
unsigned char smap_netman_irx[1];
unsigned int  size_smap_netman_irx = 1;
unsigned char socket_irx[1];
unsigned int  size_socket_irx;
int           _gp;

/* These substitutes follow the SDK declarations in network_stubs/sdk.h. */
uint32_t platform_millis(void)
{
    return clock_ms;
}

int usleep(useconds_t delay)
{
    clock_ms += delay / 1000;

    if (clock_ms >= stop_at)
    {
        test_worker_stop();
    }

    return 0;
}

int CreateSema(ee_sema_t* s)
{
    int id = ++next_semaphore;

    TEST_ASSERT_TRUE_MESSAGE(id < 8, "id < 8");

    semaphores[id] = s->init_count;

    return id;
}

int WaitSema(int id)
{
    TEST_ASSERT_TRUE_MESSAGE(semaphores[id] > 0, "semaphores[id] > 0");
    --semaphores[id];

    return 0;
}

int SignalSema(int id)
{
    ++semaphores[id];

    return 0;
}

int DeleteSema(int id)
{
    semaphores[id] = 0;

    return 0;
}

int CreateThread(ee_thread_t* t)
{
    TEST_ASSERT_NOT_NULL(t->func);

    return 10;
}

int StartThread(int id, void* arg)
{
    (void)id;
    (void)arg;

    return 0;
}

int DeleteThread(int id)
{
    (void)id;

    return 0;
}

void ExitThread(void)
{}

int scr_printf(const char* format, ...)
{
    (void)format;

    return 0;
}

int smod_get_mod_by_name(const char* name, smod_mod_info_t* info)
{
    (void)info;

    for (unsigned i = 0; i < 5; ++i)
    {
        if (!strcmp(name, module_names[i]))
        {
            return resident[i];
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(0, "0");

    return 0;
}

int sbv_patch_enable_lmb(void)
{
    ++patches;

    return 1; /* Already patched must not be treated as a startup failure. */
}

/**
 * @brief Reject ROM loads in network startup.
 * @param path Unused ROM path.
 * @param length Unused argument byte count.
 * @param args Unused argument buffer.
 * @return Failure after reporting a test assertion.
 */
int SifLoadModule(const char* path, int length, const char* args)
{
    (void)path;
    (void)length;
    (void)args;
    TEST_FAIL_MESSAGE("Network modules must use embedded images");

    return -1;
}

int SifExecModuleBuffer(void* data, unsigned size, unsigned length, const char* args, int* result)
{
    (void)size;

    void*    images[] = { ps2dev9_irx, netman_irx, smap_netman_irx, ps2ip_netman_irx, socket_irx };
    unsigned index    = 0;

    while (index < 5 && images[index] != data)
    {
        ++index;
    }

    TEST_ASSERT_TRUE_MESSAGE(index < 5 && !resident[index], "index < 5 && !resident[index]");

    for (unsigned i = 0; i < index; ++i)
    {
        TEST_ASSERT_TRUE_MESSAGE(resident[i], "resident[i]");
    }

    if (index == 3)
    {
        const char expected[] = "0.0.0.0\0"
                                "0.0.0.0\0"
                                "0.0.0.0";

        TEST_ASSERT_TRUE_MESSAGE(length == sizeof(expected) && !memcmp(args, expected, length), "args == sizeof(expected) && !memcmp(argv, expected, args)");
    }
    else
    {
        TEST_ASSERT_TRUE_MESSAGE(!length && !args, "!args && !argv");
    }

    ++loads;

    *result = start_failure == (int)index ? 1 : 0;

    if (load_failure == (int)index)
    {
        return -12;
    }

    if (!*result)
    {
        resident[index] = 1;
    }

    return 1;
}

int sceSifBindRpc(SifRpcClientData_t* client, unsigned id, int mode)
{
    TEST_ASSERT_TRUE_MESSAGE(id == PS2_RPC_SOCKET && mode == 0, "id == PS2_RPC_SOCKET && mode == 0");

    client->server = bind_failure ? NULL : client;

    return bind_failure < 0 ? -1 : 0;
}

int libcglue_ps2ip_getconfig(char* name, t_ip_info* info)
{
    ++address_queries;
    TEST_ASSERT_TRUE_MESSAGE(!strcmp(name, "sm0"), "!strcmp(name, \"sm0\")");

    *info = interface_config;

    if (interface_missing)
    {
        memset(info, 0, sizeof(*info));
    }

    return 1; /* RPC success does not guarantee that the interface exists. */
}

int libcglue_ps2ip_setconfig(const t_ip_info* info)
{
    TEST_ASSERT_TRUE_MESSAGE(!strcmp(info->netif_name, "sm0") && info->dhcp_enabled, "!strcmp(info->netif_name, \"sm0\") && info->dhcp_enabled");
    ++config_writes;

    if (config_failure)
    {
        return config_failure;
    }

    interface_config = *info;

    if (dhcp_unsupported)
    {
        interface_config.dhcp_enabled = 0;
    }

    return 1;
}

int platform_socket_bridge_compatible(void)
{
    return mock_bridge_compatible;
}

int ps2ip_init(void)
{
    return startup_failure ? -1 : 0;
}

int platform_socket_bridge_close(void)
{
    ++disconnected;

    return 0;
}

int _ps2sdk_ioctl(int fd, int command, void* value)
{
    (void)fd;
    (void)command;
    (void)value;

    return mock_ioctl_error;
}

/**
 * @brief Reset fixtures after the preceding receiver was joined.
 */
static void fixture(void)
{
    playback_hook       = NULL;
    mock_server         = NULL;
    pcm_capture         = NULL;
    capture_context     = NULL;
    current_diagnostics = NULL;
    address_queries     = 0;
    queued_artwork      = (ArtworkBlob){ 0 };

    test_worker_reset();

    for (unsigned i = 0; i < 5; ++i)
    {
        resident[i] = 1;
    }

    memset(&interface_config, 0, sizeof(interface_config));
    strcpy(interface_config.netif_name, "sm0");

    interface_config.ipaddr.s_addr = htonl(0xc0a8000a);
    loads = patches = config_writes = disconnected = 0;
    bind_failure = interface_missing = config_failure = dhcp_unsupported = 0;
    load_failure = start_failure = -1;
    mock_aria_active = mock_aria_pending = mock_ioctl_error = 0;
    mock_aria_failure = mock_stall_poll = mock_listener_failure = mock_cd_selected = 0;
    next_semaphore                                                                 = 0;
    clock_ms                                                                       = 0;
    stop_at                                                                        = 2501;
    startup_failure                                                                = 0;
    mock_bridge_compatible                                                         = 1;
}

/**
 * @brief Reject an incompatible resident bridge before changing network configuration.
 */
static void bridge_mismatch(void)
{
    char error[AUDIO_STATUS_TEXT_BYTES];

    fixture();

    mock_bridge_compatible = 0;

    TEST_ASSERT_TRUE(!(platform_network_startup(error, sizeof(error)) == 0));
    TEST_ASSERT_EQUAL_STRING("RESTART CONSOLE TO LOAD NETWORK MODULE", error);
    TEST_ASSERT_EQUAL_INT(1, disconnected);
    TEST_ASSERT_EQUAL_INT(0, config_writes);
    TEST_ASSERT_EQUAL_INT(0, loads);
}

/**
 * @brief Verify cold startup, reuse, partial initialization, failures, and retry.
 */
static void startup_tests(void)
{
    char error[48];

    fixture();
    TEST_ASSERT_TRUE_MESSAGE(platform_network_startup(error, sizeof(error)) == 0 && !error[0], "platform_network_startup(error, sizeof(error)) && !error[0]");
    TEST_ASSERT_TRUE_MESSAGE(!loads && !patches && !config_writes, "!loads && !patches && !config_writes");
    platform_network_close();

    /* A launcher can provide Ethernet without the socket RPC service. */
    fixture();

    resident[4] = 0;

    TEST_ASSERT_TRUE_MESSAGE(platform_network_startup(error, sizeof(error)) == 0 && loads == 1, "platform_network_startup(error, sizeof(error)) && loads == 1");
    TEST_ASSERT_TRUE_MESSAGE(!config_writes && interface_config.ipaddr.s_addr == htonl(0xc0a8000a), "!config_writes && interface_config.ipaddr.s_addr == htonl(0xc0a8000a)");
    platform_network_close();

    /* Existing DHCP negotiation is preserved even without a lease yet. */
    fixture();

    interface_config.ipaddr.s_addr = 0;
    interface_config.dhcp_enabled  = 1;

    TEST_ASSERT_TRUE_MESSAGE(platform_network_startup(error, sizeof(error)) == 0 && !config_writes, "platform_network_startup(error, sizeof(error)) && !config_writes");
    platform_network_close();

    /* Each prefix models a partially initialized launcher, including cold boot. */

    for (int prefix = 0; prefix < 5; ++prefix)
    {
        fixture();

        for (int i = prefix; i < 5; ++i)
        {
            resident[i] = 0;
        }

        interface_config.ipaddr.s_addr = 0;

        TEST_ASSERT_TRUE_MESSAGE(platform_network_startup(error, sizeof(error)) == 0 && !error[0], "platform_network_startup(error, sizeof(error)) && !error[0]");
        TEST_ASSERT_TRUE_MESSAGE(loads == 5 - prefix && patches == loads && config_writes == 1, "loads == 5 - prefix && patches == loads && config_writes == 1");
        platform_network_close();
        TEST_ASSERT_TRUE_MESSAGE(platform_network_startup(error, sizeof(error)) == 0 && config_writes == 1, "platform_network_startup(error, sizeof(error)) && config_writes == 1");
        TEST_ASSERT_TRUE_MESSAGE(loads == 5 - prefix, "loads == 5 - prefix");
        platform_network_close();
    }

    for (int stage = 0; stage < 5; ++stage)
    {
        for (int start = 0; start < 2; ++start)
        {
            fixture();
            memset(resident, 0, sizeof(resident));

            load_failure  = start ? -1 : stage;
            start_failure = start ? stage : -1;

            TEST_ASSERT_TRUE_MESSAGE(platform_network_startup(error, sizeof(error)) != 0, "!platform_network_startup(error, sizeof(error))");
            TEST_ASSERT_TRUE_MESSAGE(strstr(error, module_names[stage]) && loads == stage + 1, "strstr(error, module_names[stage]) && loads == stage + 1");
            TEST_ASSERT_TRUE_MESSAGE(!disconnected, "!disconnected");

            load_failure = start_failure = -1;

            TEST_ASSERT_TRUE_MESSAGE(platform_network_startup(error, sizeof(error)) == 0 && !error[0], "platform_network_startup(error, sizeof(error)) && !error[0]");
            platform_network_close();
        }
    }

    for (int failure = 0; failure < 6; ++failure)
    {
        fixture();

        interface_config.ipaddr.s_addr = 0;
        bind_failure                   = failure == 0 ? 1 : failure == 1 ? -1
                                                                         : 0;
        interface_missing              = failure == 2;
        config_failure                 = failure == 3 ? -1 : 0;
        dhcp_unsupported               = failure == 4;
        startup_failure                = failure == 5;

        TEST_ASSERT_TRUE_MESSAGE(platform_network_startup(error, sizeof(error)) != 0 && error[0], "!platform_network_startup(error, sizeof(error)) && error[0]");
        TEST_ASSERT_TRUE_MESSAGE(disconnected == (failure >= 2), "disconnected == (failure >= 2)");
        TEST_ASSERT_TRUE_MESSAGE(clock_ms <= 1000, "clock_ms <= 1000");

        bind_failure = interface_missing = config_failure = dhcp_unsupported = startup_failure = 0;

        TEST_ASSERT_TRUE_MESSAGE(platform_network_startup(error, sizeof(error)) == 0 && !error[0], "platform_network_startup(error, sizeof(error)) && !error[0]");
        platform_network_close();
    }
}

static void run_once(void)
{
    stop_at = clock_ms + 1;

    test_worker_run();
}

static void send_sample(void)
{
    const uint8_t pcm[] = { 17, 0, 17, 0 };

    pcm_capture(capture_context, pcm, 1);
}

static void send_replacement_sample(void)
{
    Audio         audio;
    TrackMetadata metadata;
    unsigned      generation;

    TEST_ASSERT_EQUAL_INT(1, network_copy_snapshot(&generation, &audio, &metadata));
    TEST_ASSERT_EQUAL_UINT(9, generation);
    TEST_ASSERT_EQUAL_UINT(0, audio.history_count);
    TEST_ASSERT_EQUAL_STRING("New session", metadata.title);

    const uint8_t pcm[] = { 42, 0, 42, 0 };

    pcm_capture(capture_context, pcm, 1);
}

static void clear_samples(void)
{
    pcm_capture(capture_context, NULL, 0);
}

static void network_regressions(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);
    run_once();
    TEST_ASSERT_EQUAL_INT(0, network_poll(&observed_generation));
    TEST_ASSERT_EQUAL_STRING("", network_error());
    TEST_ASSERT_EQUAL_INT(0, network_close());
    fixture();
    TEST_ASSERT_TRUE(network_open() == 0);

    mock_aria_active               = 1;
    mock_server->stream.generation = 7;

    run_once();
    TEST_ASSERT_EQUAL_INT(1, network_poll(&observed_generation));
    TEST_ASSERT_EQUAL_UINT(7, observed_generation);

    mock_server->stream.generation = 9;

    TEST_ASSERT_EQUAL_INT(1, network_poll(&observed_generation));
    TEST_ASSERT_EQUAL_UINT(7, observed_generation);
    TEST_ASSERT_EQUAL_INT(0, network_close());
    fixture();
    TEST_ASSERT_TRUE(network_open() == 0);

    Audio         audio;
    TrackMetadata metadata;

    TEST_ASSERT_EQUAL_INT(0, network_copy_snapshot(&observed_generation, &audio, &metadata));
    TEST_ASSERT_EQUAL_UINT(0, audio.history_count);
    TEST_ASSERT_EQUAL_INT(0, network_close());

    startup_failure = 1;

    TEST_ASSERT_TRUE(!(network_open() == 0));
    TEST_ASSERT_EQUAL_INT(NETWORK_ERROR_UNAVAILABLE, network_poll(&observed_generation));
    TEST_ASSERT_NOT_EQUAL(0, network_error()[0]);
    fixture();
    TEST_ASSERT_TRUE(network_open() == 0);
    TEST_ASSERT_EQUAL_STRING("", network_error());
    TEST_ASSERT_EQUAL_INT(0, network_close());
    fixture();
    memset(resident, 0, sizeof(resident));

    interface_config.ipaddr.s_addr = 0;

    TEST_ASSERT_TRUE(network_open() == 0);
    TEST_ASSERT_EQUAL_STRING("", network_address());

    interface_config.ipaddr.s_addr = htonl(0x0a00002a);
    stop_at                        = 2501;

    test_worker_run();
    TEST_ASSERT_EQUAL_INT(0, network_poll(&observed_generation));
    TEST_ASSERT_EQUAL_STRING("10.0.0.42", network_address());
}

static void stalled_worker(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);

    mock_stall_poll = 1;
    stop_at         = 2001;

    test_worker_run();
    TEST_ASSERT_FALSE(mock_stall_poll);
    TEST_ASSERT_EQUAL_INT(0, network_poll(&observed_generation));
    TEST_ASSERT_EQUAL_STRING("", network_error());
}

static void listener_failure(void)
{
    mock_aria_failure = 1;

    TEST_ASSERT_TRUE(!(network_open() == 0));
    TEST_ASSERT_EQUAL_INT(NETWORK_ERROR_UNAVAILABLE, network_poll(&observed_generation));
    TEST_ASSERT_EQUAL_STRING("ARIA TCP 12889 SOCKET ERR 23", network_error());
    TEST_ASSERT_EQUAL_INT(1, disconnected);
    fixture();
    TEST_ASSERT_TRUE(network_open() == 0);

    mock_listener_failure = 1;

    run_once();
    TEST_ASSERT_EQUAL_INT(NETWORK_ERROR_RECEIVER, network_poll(&observed_generation));
    TEST_ASSERT_EQUAL_STRING("ARIA LISTENER FAILED", network_error());
}

static void source_ownership(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);

    mock_aria_pending = 1;

    run_once();
    TEST_ASSERT_EQUAL_INT(0, network_poll(&observed_generation));
    TEST_ASSERT_EQUAL_INT(7, mock_server->audio_fd);
    TEST_ASSERT_EQUAL_INT(0, network_close());
    fixture();
    TEST_ASSERT_TRUE(network_open() == 0);

    mock_cd_selected = mock_aria_active = 1;
    mock_server->stream.generation      = 7;

    run_once();
    TEST_ASSERT_EQUAL_INT(0, network_poll(&observed_generation));
    TEST_ASSERT_EQUAL_INT(-1, mock_server->audio_fd);
}

static void queue_cover(const char* title, const char* url)
{
    queued_artwork = (ArtworkBlob){ .data = malloc(4), .size = 4 };

    TEST_ASSERT_NOT_NULL(queued_artwork.data);
    memcpy(queued_artwork.data, "COVR", 4);
    strcpy(queued_artwork.metadata.title, title);
    strcpy(queued_artwork.metadata.artwork_url, url);
}

static void publish_track(const char* title, const char* url)
{
    mock_aria_active = 1;

    strcpy(mock_server->metadata.title, title);
    strcpy(mock_server->metadata.artwork_url, url);
    run_once();
}

static void snapshot_metadata(TrackMetadata* metadata)
{
    Audio audio;

    TEST_ASSERT_EQUAL_INT(1, network_copy_snapshot(&observed_generation, &audio, metadata));
}

static void artwork_handoff(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);
    publish_track("Track", "http://1.2.3.4/cover");
    queue_cover("Track", "http://1.2.3.4/cover");
    run_once();

    TrackMetadata metadata;

    snapshot_metadata(&metadata);

    /* The UI can poll after several worker steps without a new cover. */
    run_once();
    run_once();

    ArtworkBlob blob = { 0 };

    TEST_ASSERT_TRUE(network_take_artwork(&metadata, &blob) == 0);
    TEST_ASSERT_EQUAL_UINT(4, blob.size);
    TEST_ASSERT_EQUAL_MEMORY("COVR", blob.data, 4);
    TEST_ASSERT_TRUE(!(network_take_artwork(&metadata, &blob) == 0));
    free(blob.data);
}

static void artwork_handoff_track_change(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);
    publish_track("First track", "http://1.2.3.4/cover");

    TrackMetadata displayed;

    snapshot_metadata(&displayed);
    queue_cover("Second track", "http://1.2.3.4/cover");

    uint8_t* pending = queued_artwork.data;

    publish_track("Second track", "http://1.2.3.4/cover");

    ArtworkBlob blob = { 0 };

    TEST_ASSERT_TRUE(!(network_take_artwork(&displayed, &blob) == 0));
    TEST_ASSERT_NULL(blob.data);
    snapshot_metadata(&displayed);
    TEST_ASSERT_TRUE(network_take_artwork(&displayed, &blob) == 0);
    TEST_ASSERT_EQUAL_PTR(pending, blob.data);
    TEST_ASSERT_EQUAL_STRING("Second track", blob.metadata.title);
    TEST_ASSERT_TRUE(!(network_take_artwork(&displayed, &blob) == 0));
    free(blob.data);
}

static void artwork_handoff_old_cover(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);
    queue_cover("First track", "http://1.2.3.4/old");
    publish_track("First track", "http://1.2.3.4/old");

    TrackMetadata displayed;

    snapshot_metadata(&displayed);
    publish_track("Second track", "http://1.2.3.4/new");

    ArtworkBlob blob = { 0 };

    TEST_ASSERT_TRUE(!(network_take_artwork(&displayed, &blob) == 0));
    snapshot_metadata(&displayed);
    TEST_ASSERT_TRUE(!(network_take_artwork(&displayed, &blob) == 0));
    TEST_ASSERT_NULL(blob.data);
}

static void reconnect_snapshot(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);

    mock_server->stream.generation = 7;
    playback_hook                  = send_sample;

    publish_track("Old session", "");
    TEST_ASSERT_EQUAL_INT(1, network_poll(&observed_generation));
    TEST_ASSERT_EQUAL_UINT(7, observed_generation);

    mock_server->stream.generation = 9;
    playback_hook                  = send_replacement_sample;

    publish_track("New session", "");

    Audio         audio;
    TrackMetadata metadata;

    TEST_ASSERT_EQUAL_INT(1, network_copy_snapshot(&observed_generation, &audio, &metadata));
    TEST_ASSERT_EQUAL_UINT(9, observed_generation);
    TEST_ASSERT_EQUAL_UINT(1, audio.history_count);
    TEST_ASSERT_EQUAL_INT16(42, audio.history[0][0]);

    playback_hook = NULL;

    run_once();
    TEST_ASSERT_EQUAL_INT(1, network_copy_snapshot(&observed_generation, &audio, &metadata));
    TEST_ASSERT_EQUAL_UINT(1, audio.history_count);
    ++mock_server->stream.generation;

    mock_aria_active = 0;

    memset(&mock_server->metadata, 0, sizeof(mock_server->metadata));

    playback_hook = clear_samples;

    run_once();
    TEST_ASSERT_EQUAL_INT(0, network_copy_snapshot(&observed_generation, &audio, &metadata));
    TEST_ASSERT_EQUAL_UINT(10, observed_generation);
    TEST_ASSERT_EQUAL_UINT(0, audio.history_count);
    TEST_ASSERT_EQUAL_STRING("", metadata.title);
    TEST_ASSERT_EQUAL_INT(0, network_close());
    TEST_ASSERT_EQUAL_INT(NETWORK_ERROR_UNAVAILABLE, network_copy_snapshot(&observed_generation, &audio, &metadata));
}

static void capture_after_rpc(void)
{
    clock_ms += 25;

    send_sample();
    test_worker_stop();
}

static void address_refresh_after_capture(void)
{
    const uint32_t starts[] = { 3000, UINT32_MAX - 10 };

    for (unsigned i = 0; i < 2; ++i)
    {
        TEST_ASSERT_TRUE(network_open() == 0);

        mock_aria_active = 1;
        clock_ms         = starts[i];
        playback_hook    = capture_after_rpc;
        unsigned before  = address_queries;

        test_worker_run();
        TEST_ASSERT_EQUAL_UINT(before, address_queries);

        playback_hook    = NULL;
        mock_aria_active = 0;
        clock_ms += AUDIO_STALE_MS;

        run_once();
        TEST_ASSERT_GREATER_THAN_UINT(before, address_queries);
        TEST_ASSERT_EQUAL_INT(0, network_close());
    }
}

#if STROOM_DIAGNOSTICS
static void artwork_display_identity(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);
    publish_track("First track", "http://1.2.3.4/cover");

    TrackMetadata old;

    snapshot_metadata(&old);
    publish_track("Second track", "http://1.2.3.4/cover");

    TrackMetadata current;

    snapshot_metadata(&current);
    network_artwork_display(&current, "WAITING FOR DOWNLOAD");
    network_artwork_display(&old, "DRAWN");
    run_once();
    TEST_ASSERT_EQUAL_STRING("WAITING FOR DOWNLOAD", current_diagnostics->artwork_display);
    network_artwork_display(&current, "DRAWN");
    run_once();
    TEST_ASSERT_EQUAL_STRING("DRAWN", current_diagnostics->artwork_display);
}

static void cover_observation_mailbox(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);

    mock_server->stream.generation = 7;
    mock_server->metadata_revision = 3;

    publish_track("Cover A", "");

    TrackMetadata current;

    snapshot_metadata(&current);

    mock_server->metadata_revision = 4;
    ArtworkObservation observation = { DIAGNOSTIC_ARTWORK_DECODE_END, UINT32_MAX - 2, 3, { 1024, 512, 0x101 } };

    network_artwork_observe(&current, observation);

    TrackMetadata stale = current;

    strcpy(stale.title, "Old");
    network_artwork_observe(&stale, observation);
    TEST_ASSERT_EQUAL_UINT(0, current_diagnostics->diagnostic_capture.count);
    run_once();

    AriaDiagnosticCaptureRecord record = current_diagnostics->diagnostic_capture.rolling[0];

    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_CAPTURE_COVER, record.kind);
    TEST_ASSERT_EQUAL_UINT(6, record.data[DIAGNOSTIC_CAPTURE_COVER_DURATION_MS]);
    TEST_ASSERT_EQUAL_UINT(3, record.data[DIAGNOSTIC_CAPTURE_COVER_METADATA_REVISION]);
    TEST_ASSERT_EQUAL_UINT(1024, record.data[DIAGNOSTIC_CAPTURE_COVER_VALUE0]);

    for (unsigned i = 0; i < 100; ++i)
    {
        network_artwork_observe(&current, observation);
    }

    run_once();

    record = current_diagnostics->diagnostic_capture.rolling[current_diagnostics->diagnostic_capture.write - 1];

    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_ARTWORK_OBSERVATIONS_LOST, record.data[DIAGNOSTIC_CAPTURE_COVER_PHASE]);
    TEST_ASSERT_GREATER_THAN_UINT(0, record.data[DIAGNOSTIC_CAPTURE_COVER_VALUE0]);
    network_artwork_observe(&current, observation);
    ++mock_server->stream.generation;
    run_once();

    record = current_diagnostics->diagnostic_capture.rolling[current_diagnostics->diagnostic_capture.write - 1];

    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_ARTWORK_OBSERVATIONS_LOST, record.data[DIAGNOSTIC_CAPTURE_COVER_PHASE]);
}
#endif
static void listening_skips_artwork(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);

    mock_server->stream.listening = 1;

    strcpy(mock_server->stream.device_name, "Focusrite 18i20");
    queue_cover("Track", "http://example.test/cover.jpg");
    publish_track("Track", "http://example.test/cover.jpg");

    TrackMetadata current;

    snapshot_metadata(&current);

    ArtworkBlob blob = { 0 };

    TEST_ASSERT_TRUE(!(network_take_artwork(&current, &blob) == 0));
    TEST_ASSERT_FALSE(artwork_requested);
    TEST_ASSERT_EQUAL_STRING("Focusrite 18i20", network_device_name());

    mock_server->stream.listening = 0;

    run_once();
    TEST_ASSERT_TRUE(artwork_requested);
}

static void failed_join_rejects_reopen(void)
{
    TEST_ASSERT_TRUE(network_open() == 0);
    publish_track("Retained track", "");

    status_failure  = 1;
    int      before = disconnected;
    unsigned opens  = test_worker_opens();

    TEST_ASSERT_EQUAL_INT(NETWORK_ERROR_WORKER_CLOSE, network_close());
    TEST_ASSERT_TRUE(!(network_open() == 0));
    TEST_ASSERT_EQUAL_UINT(opens, test_worker_opens());
    TEST_ASSERT_EQUAL_INT(before, disconnected);

    status_failure = 0;

    TEST_ASSERT_EQUAL_INT(0, network_close());
    TEST_ASSERT_EQUAL_INT(before + 1, disconnected);
    TEST_ASSERT_TRUE(network_open() == 0);

    Audio         audio;
    TrackMetadata metadata;

    TEST_ASSERT_EQUAL_INT(0, network_copy_snapshot(&observed_generation, &audio, &metadata));
    TEST_ASSERT_EQUAL_STRING("", metadata.title);
}

void setUp(void)
{
    fixture();
}

void tearDown(void)
{
    status_failure = 0;

    test_worker_stop();
    TEST_ASSERT_EQUAL_INT(0, network_close());
    free(queued_artwork.data);
}

int ReferThreadStatus(int id, ee_thread_status_t* status)
{
    (void)id;

    if (status_failure)
    {
        return -1;
    }

    status->status = THS_DORMANT;

    return 0;
}

void platform_sleep_us(uint32_t microseconds)
{
    (void)usleep(microseconds);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(failed_join_rejects_reopen);
#if STROOM_DIAGNOSTICS
    RUN_TEST(artwork_display_identity);
    RUN_TEST(cover_observation_mailbox);
#endif
    RUN_TEST(reconnect_snapshot);
    RUN_TEST(address_refresh_after_capture);
    RUN_TEST(artwork_handoff);
    RUN_TEST(listening_skips_artwork);
    RUN_TEST(artwork_handoff_track_change);
    RUN_TEST(artwork_handoff_old_cover);
    RUN_TEST(bridge_mismatch);
    RUN_TEST(startup_tests);
    RUN_TEST(network_regressions);
    RUN_TEST(source_ownership);
    RUN_TEST(listener_failure);
    RUN_TEST(stalled_worker);

    return UNITY_END();
}
