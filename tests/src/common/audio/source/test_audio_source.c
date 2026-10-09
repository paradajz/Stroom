#include "audio/source/source.h"
#include "audio/network/network.h"
#include "audio/cd/cd.h"
#include "audio/output/device.h"
#include "unity.h"
#include "support/process.h"
#include <string.h>

static CdPlaybackStatus disc;
static int              cd_open_ok, cd_available, output_open_ok, output_opens;
static uint32_t         clock_ms;

/**
 * @brief Substitute platform_millis in this host fixture.
 */
uint32_t platform_millis(void)
{
    return clock_ms;
}

static int                    configured_autoplay, configured_mute;
static int                    network_opened, network_closed, cd_opened, cd_closed, network_polls;
static int                    active, listening, network_ok, disconnect_on_copy, eject_on_copy;
static int                    network_restart_required;
static int                    fail_on_copy;
static int                    cd_close_ok, network_close_ok, output_closed, output_close_ok;
static unsigned               session_generation, replacement_generation;
static int                    cd_close_pending, network_close_pending;
static OutputOwner            sound_owner;
static unsigned               transport_calls, transport_generation;
static AudioTransportRequests transport_requests;

/**
 * @brief Substitute cd_transport_apply in this host fixture.
 */
void cd_transport_apply(unsigned generation, const AudioTransportRequests* requests)
{
    ++transport_calls;

    transport_generation = generation;
    transport_requests   = *requests;
}

/**
 * @brief Substitute output_open in this host fixture.
 */
int output_open(void)
{
    ++output_opens;

    sound_owner = OUTPUT_NONE;

    return output_open_ok ? 0 : OUTPUT_ERROR_LOCK_CREATE;
}

/**
 * @brief Substitute output_select in this host fixture.
 */
void output_select(OutputOwner owner)
{
    sound_owner = owner;
}

/**
 * @brief Substitute output_close in this host fixture.
 */
int output_close(void)
{
    ++output_closed;

    sound_owner = OUTPUT_NONE;

    return output_close_ok ? 0 : -1;
}

/**
 * @brief Substitute output_set_muted in this host fixture.
 */
void output_set_muted(int muted)
{
    configured_mute = muted;
}

/**
 * @brief Substitute cd_open in this host fixture.
 */
int cd_open(int autoplay)
{
    configured_autoplay = autoplay;

    ++cd_opened;

    return cd_open_ok ? 0 : -1;
}

/**
 * @brief Substitute cd_close in this host fixture.
 */
int cd_close(void)
{
    ++cd_closed;

    return !cd_close_ok ? -1 : cd_close_pending ? 1
                                                : 0;
}

/**
 * @brief Substitute cd_poll in this host fixture.
 */
int cd_poll(CdPlaybackStatus* status)
{
    *status = disc;

    return status->restart_required ? CD_ERROR_RESTART_REQUIRED : cd_available ? 0
                                                                               : CD_ERROR_WORKER_UNAVAILABLE;
}

/**
 * @brief Substitute cd_copy_snapshot in this host fixture.
 */
void cd_copy_snapshot(CdPlaybackStatus* status, Audio* audio)
{
    if (eject_on_copy)
    {
        disc.present = 0;
        disc.playing = 0;
    }

    *status = disc;

    memset(audio, 0, sizeof(*audio));

    audio->active  = disc.playing;
    audio->peak[0] = disc.present ? 0.75f : 0;
}

/**
 * @brief Substitute network_open in this host fixture.
 */
int network_open(void)
{
    ++network_opened;

    if (network_restart_required)
    {
        return NETWORK_START_RESTART_REQUIRED;
    }

    return network_ok ? 0 : -1;
}

/**
 * @brief Substitute network_close in this host fixture.
 */
int network_close(void)
{
    ++network_closed;

    return !network_close_ok ? -1 : network_close_pending ? 1
                                                          : 0;
}

/**
 * @brief Substitute network_listening in this host fixture.
 */
int network_listening(void)
{
    return listening;
}

/**
 * @brief Substitute network_device_name in this host fixture.
 */
const char* network_device_name(void)
{
    return listening ? "Focusrite 18i20" : "";
}

/**
 * @brief Substitute network_address in this host fixture.
 */
const char* network_address(void)
{
    return "192.168.1.240";
}

/**
 * @brief Substitute network_error in this host fixture.
 */
const char* network_error(void)
{
    if (network_restart_required)
    {
        return "RESTART CONSOLE TO LOAD NETWORK MODULE";
    }

    return network_ok ? "" : "NETWORK ERROR";
}

/**
 * @brief Substitute network_poll in this host fixture.
 */
int network_poll(unsigned* generation)
{
    ++network_polls;

    *generation = session_generation;

    return network_ok ? active : -1;
}

/**
 * @brief Substitute network_copy_snapshot in this host fixture.
 */
int network_copy_snapshot(unsigned* generation, Audio* audio, TrackMetadata* metadata)
{
    if (replacement_generation)
    {
        session_generation = replacement_generation;
    }

    if (disconnect_on_copy)
    {
        active = 0;

        ++session_generation;
    }

    *generation = session_generation;

    memset(audio, 0, sizeof(*audio));
    memset(metadata, 0, sizeof(*metadata));

    if (active)
    {
        audio->active  = 1;
        audio->peak[0] = 0.25f;

        strcpy(metadata->title, "Network track");
    }

    return fail_on_copy ? -1 : active;
}

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    network_close_pending = 0;
    cd_close_pending      = 0;
    output_open_ok        = 1;
    cd_available          = 1;
    cd_open_ok            = 1;
    clock_ms              = 0;
    transport_calls       = 0;

    memset(&disc, 0, sizeof(disc));

    network_polls            = 0;
    cd_closed                = 0;
    cd_opened                = 0;
    network_closed           = 0;
    network_opened           = 0;
    eject_on_copy            = 0;
    disconnect_on_copy       = 0;
    listening                = 0;
    active                   = 0;
    session_generation       = 1;
    replacement_generation   = 0;
    network_ok               = 1;
    network_restart_required = 0;
    fail_on_copy             = 0;
    output_close_ok          = 1;
    network_close_ok         = 1;
    cd_close_ok              = 1;
    output_opens             = 0;
    output_closed            = 0;

    TEST_ASSERT_EQUAL_INT(0, audio_source_open(1, 0));
}

/**
 * @brief Finish fixture cleanup and resource checks after each test.
 */
void tearDown(void)
{
    network_close_pending = 0;
    cd_close_pending      = 0;
    network_close_ok      = 1;
    cd_close_ok           = 1;

    TEST_ASSERT_TRUE(audio_source_close() == 0);
}

/**
 * @brief Check receiver availability before its first published session.
 */
static void receiver_waits_for_first_poll(int silent)
{
    Audio             audio;
    AudioSourceStatus status;

    active    = 1;
    listening = silent;
    clock_ms += 5000;

    TEST_ASSERT_EQUAL_INT(0, network_opened);
    TEST_ASSERT_EQUAL_INT(0, network_polls);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_FALSE(status.network_waiting);
    TEST_ASSERT_EQUAL_INT(silent, status.listening);
    TEST_ASSERT_EQUAL_INT(silent ? OUTPUT_NONE : OUTPUT_NETWORK, sound_owner);
}

/**
 * @brief Verify streaming receiver starts only after first poll.
 */
static void streaming_receiver_starts_only_after_first_poll(void)
{
    receiver_waits_for_first_poll(0);
}

/**
 * @brief Verify listening receiver starts only after first poll.
 */
static void listening_receiver_starts_only_after_first_poll(void)
{
    receiver_waits_for_first_poll(1);
}

/**
 * @brief Verify startup cd keeps network inactive.
 */
static void startup_cd_keeps_network_inactive(void)
{
    Audio             audio;
    AudioSourceStatus status;

    disc.checking = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CHECKING, status.kind);
    TEST_ASSERT_EQUAL_INT(0, network_opened);
    TEST_ASSERT_EQUAL_INT(0, network_polls);

    disc.checking = 0;
    disc.playing  = 1;
    disc.present  = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, status.kind);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_CD, sound_owner);

    disc.playing = 0;
    disc.paused  = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, status.kind);
    TEST_ASSERT_EQUAL_INT(0, network_opened);
    TEST_ASSERT_EQUAL_INT(0, network_polls);

    disc.present = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(0, cd_closed);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
}

/**
 * @brief Verify no cd selects network once.
 */
static void no_cd_selects_network_once(void)
{
    Audio             audio;
    AudioSourceStatus status;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_TRUE(status.network_ready);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_EQUAL_STRING("192.168.1.240", status.network_address);
    TEST_ASSERT_EQUAL_INT(0, cd_closed);

    active = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NETWORK, sound_owner);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_STRING("Network track", status.metadata.title);

    disc.present = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);

    disc.present = 0;
    active       = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_STRING("", status.metadata.title);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
    TEST_ASSERT_EQUAL_INT(2, cd_opened);
}

/**
 * @brief Verify listening analyzes without output ownership.
 */
static void listening_analyzes_without_output_ownership(void)
{
    Audio             audio;
    AudioSourceStatus status;

    listening = 1;
    active    = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_TRUE(status.listening);
    TEST_ASSERT_EQUAL_STRING("Focusrite 18i20", status.device_name);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_FALSE(status.network_waiting);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
    TEST_ASSERT_EQUAL_STRING("Network track", status.metadata.title);
}

/**
 * @brief Verify snapshots handle disc and session changes.
 */
static void snapshots_handle_disc_and_session_changes(void)
{
    Audio             audio;
    AudioSourceStatus status;

    eject_on_copy          = 1;
    disc.present           = 1;
    active                 = 1;
    replacement_generation = 17;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_EQUAL_UINT(17, status.network_generation);

    disconnect_on_copy = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
}

/**
 * @brief Verify unavailable sources report network failure.
 */
static void unavailable_sources_report_network_failure(void)
{
    Audio             audio;
    AudioSourceStatus status;

    strcpy(disc.error, "CD UNAVAILABLE");

    network_ok = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_FALSE(status.network_ready);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_INT(0, network_polls);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
}

/**
 * @brief Verify tray close on waiting screen selects cd.
 */
static void tray_close_on_waiting_screen_selects_cd(void)
{
    Audio             audio;
    AudioSourceStatus status;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_EQUAL_INT(0, cd_closed);

    disc.checking = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_TRUE(status.network_waiting);

    disc.checking = 0;
    disc.playing  = 1;
    disc.present  = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, status.kind);
    TEST_ASSERT_EQUAL_INT(OUTPUT_CD, sound_owner);
    TEST_ASSERT_FALSE(status.network_waiting);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_INT(1, network_closed);
    TEST_ASSERT_EQUAL_INT(1, cd_opened);

    disc.playing = 0;
    disc.present = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_EQUAL_INT(2, network_opened);

    disc.playing = 1;
    disc.present = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, status.kind);
    TEST_ASSERT_EQUAL_INT(2, network_closed);
}

/**
 * @brief Verify active stream defers disc until disconnect.
 */
static void active_stream_defers_disc_until_disconnect(void)
{
    Audio             audio;
    AudioSourceStatus status;

    for (int silent = 0; silent < 2; ++silent)
    {
        disc.playing = 0;
        disc.present = 0;
        active       = 1;
        listening    = silent;

        audio_source_poll(&audio, &status);

        disc.playing = 1;
        disc.present = 1;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
        TEST_ASSERT_EQUAL_INT(silent ? OUTPUT_NONE : OUTPUT_NETWORK, sound_owner);

        active = 0;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, status.kind);
        TEST_ASSERT_FALSE(status.listening);
        TEST_ASSERT_EQUAL_INT(OUTPUT_CD, sound_owner);
    }
}

/**
 * @brief Verify startup audio preferences.
 */
static void startup_audio_preferences(void)
{
    Audio             audio;
    AudioSourceStatus status;

    audio_source_close();
    audio_source_open(0, 1);
    TEST_ASSERT_FALSE(configured_autoplay);
    TEST_ASSERT_TRUE(configured_mute);

    active = 1;

    audio_source_poll(&audio, &status);

    active              = 0;
    configured_autoplay = -1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_FALSE(configured_autoplay);
}

/**
 * @brief Verify transport routes only to selected source.
 */
static void transport_routes_only_to_selected_source(void)
{
    AudioSourceStatus            status   = { .kind = AUDIO_SOURCE_CD, .cd = { .generation = 42 } };
    const AudioTransportRequests requests = {
        .commands = { AUDIO_TRANSPORT_STOP }, .command_count = 1, .program = { 2, 1 }, .program_count = 2, .scan_direction = -1
    };

    audio_source_apply(&status, &requests);
    TEST_ASSERT_EQUAL_UINT(1, transport_calls);
    TEST_ASSERT_EQUAL_UINT(42, transport_generation);
    TEST_ASSERT_EQUAL_MEMORY(&requests, &transport_requests, sizeof(requests));

    status.kind = AUDIO_SOURCE_NETWORK;

    audio_source_apply(&status, &requests);

    status.kind = AUDIO_SOURCE_CHECKING;

    audio_source_apply(&status, &requests);
    TEST_ASSERT_EQUAL_UINT(1, transport_calls);

    status.kind          = AUDIO_SOURCE_CD;
    status.cd.generation = 43;

    const AudioTransportRequests release = { 0 };

    audio_source_apply(&status, &release);
    TEST_ASSERT_EQUAL_UINT(2, transport_calls);
    TEST_ASSERT_EQUAL_UINT(43, transport_generation);
    TEST_ASSERT_EQUAL_INT(0, transport_requests.scan_direction);
}

/** @brief A stopped stream cannot trigger CD reopening while its old join is pending. */
static void cd_join_failure_defers_selection(void)
{
    Audio             audio;
    AudioSourceStatus status;
    active      = 1;
    cd_close_ok = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("CD CLEANUP FAILED", status.error);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
    TEST_ASSERT_EQUAL_INT(1, cd_closed);

    active = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(2, cd_closed);
    TEST_ASSERT_EQUAL_INT(1, cd_opened);
    TEST_ASSERT_EQUAL_INT(1, network_polls);

    cd_close_ok = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(3, cd_closed);
    TEST_ASSERT_EQUAL_INT(2, cd_opened);
    TEST_ASSERT_TRUE(status.network_waiting);
}

/** @brief Receiver cleanup must finish before CD selection or a new receiver starts. */
static void network_join_failure_defers_selection(void)
{
    Audio             audio;
    AudioSourceStatus status;

    audio_source_poll(&audio, &status);

    disc.playing     = 1;
    disc.present     = 1;
    network_close_ok = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("NETWORK CLEANUP FAILED", status.error);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
    TEST_ASSERT_EQUAL_INT(1, network_closed);

    disc.playing = 0;
    disc.present = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(2, network_closed);
    TEST_ASSERT_EQUAL_INT(1, network_opened);

    network_close_ok = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(3, network_closed);
    TEST_ASSERT_EQUAL_INT(2, network_opened);
    TEST_ASSERT_TRUE(status.network_waiting);
}

/** @brief A pending CD stop is neutral, preserves ownership, and resumes selection after exit. */
static void pending_cd_cleanup_is_waiting(void)
{
    Audio             audio;
    AudioSourceStatus status;

    active           = 1;
    cd_close_pending = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_WAITING, status.kind);
    TEST_ASSERT_EQUAL_STRING("STOPPING CD WORKER", status.error);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);

    active = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_WAITING, status.kind);
    TEST_ASSERT_EQUAL_STRING("STOPPING CD WORKER", status.error);
    TEST_ASSERT_EQUAL_INT(1, cd_opened);
    TEST_ASSERT_EQUAL_INT(1, network_polls);

    cd_close_ok = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);

    cd_close_ok      = 1;
    cd_close_pending = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_EQUAL_INT(2, cd_opened);
}

/** @brief Eject during pending receiver cleanup stays neutral and allows subsequent CD playback. */
static void pending_network_cleanup_is_waiting(void)
{
    Audio             audio;
    AudioSourceStatus status;

    audio_source_poll(&audio, &status);

    disc.playing          = 1;
    disc.present          = 1;
    network_close_pending = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_WAITING, status.kind);
    TEST_ASSERT_EQUAL_STRING("STOPPING NETWORK RECEIVER", status.error);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);

    disc.playing = 0;
    disc.present = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_WAITING, status.kind);
    TEST_ASSERT_EQUAL_STRING("STOPPING NETWORK RECEIVER", status.error);
    TEST_ASSERT_EQUAL_INT(1, network_opened);

    network_close_pending = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_EQUAL_INT(2, network_opened);

    disc.playing = 1;
    disc.present = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, status.kind);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_CD, sound_owner);
}

/** @brief Failed shutdown retains output resources and rejects preference/state resets. */
static void failed_shutdown_preserves_ownership(void)
{
    Audio             audio;
    AudioSourceStatus status;

    audio_source_poll(&audio, &status);

    network_close_ok = 0;
    cd_close_ok      = 0;

    TEST_ASSERT_TRUE(!(audio_source_close() == 0));
    TEST_ASSERT_EQUAL_INT(0, output_closed);
    TEST_ASSERT_EQUAL_INT(1, cd_closed);
    TEST_ASSERT_EQUAL_INT(1, network_closed);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR_CD_CLOSE, audio_source_open(0, 1));
    TEST_ASSERT_EQUAL_INT(1, cd_opened);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
    TEST_ASSERT_EQUAL_INT(1, configured_autoplay);
    TEST_ASSERT_EQUAL_INT(0, configured_mute);
    TEST_ASSERT_EQUAL_INT(2, cd_closed);
    TEST_ASSERT_EQUAL_INT(2, network_closed);

    network_close_ok = 1;
    cd_close_ok      = 1;

    TEST_ASSERT_TRUE(audio_source_close() == 0);
    TEST_ASSERT_EQUAL_INT(1, output_closed);
    TEST_ASSERT_TRUE(audio_source_close() == 0);
    TEST_ASSERT_EQUAL_INT(3, cd_closed);
    TEST_ASSERT_EQUAL_INT(3, network_closed);
}

/**
 * @brief Verify prolonged cd cleanup retains resources.
 */
static void prolonged_cd_cleanup_retains_resources(void)
{
    Audio             audio;
    AudioSourceStatus status;
    clock_ms         = UINT32_MAX - 5000;
    cd_close_pending = 1;
    active           = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_WAITING, status.kind);
    TEST_ASSERT_EQUAL_STRING("STOPPING CD WORKER", status.error);

    clock_ms += 4999;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_WAITING, status.kind);

    ++clock_ms;

    for (unsigned i = 0; i < 3; ++i)
    {
        cd_close_ok = i != 1; /* Pending and failed attempts share the same wait. */

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
        TEST_ASSERT_EQUAL_STRING("CANNOT STOP AUDIO - RESTART CONSOLE", status.error);
        TEST_ASSERT_FALSE(audio.active);
        TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
        TEST_ASSERT_EQUAL_INT(1, cd_opened);
        TEST_ASSERT_EQUAL_INT(0, output_closed);

        clock_ms += 2000;
    }

    TEST_ASSERT_EQUAL_INT(5, cd_closed);

    cd_close_pending = 0;
    active           = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_EQUAL_STRING("", status.error);
    TEST_ASSERT_EQUAL_INT(2, cd_opened);

    /* A new cleanup gets its own full wait, rather than inheriting the warning. */
    disc.playing          = 1;
    disc.present          = 1;
    network_close_pending = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_WAITING, status.kind);
    TEST_ASSERT_EQUAL_STRING("STOPPING NETWORK RECEIVER", status.error);
}

/**
 * @brief Verify prolonged network cleanup retains resources.
 */
static void prolonged_network_cleanup_retains_resources(void)
{
    Audio             audio;
    AudioSourceStatus status;

    audio_source_poll(&audio, &status);

    disc.playing     = 1;
    disc.present     = 1;
    network_close_ok = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_STRING("NETWORK CLEANUP FAILED", status.error);

    clock_ms += 5000;

    for (unsigned i = 0; i < 3; ++i)
    {
        network_close_ok      = i != 1;
        network_close_pending = 1;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
        TEST_ASSERT_EQUAL_STRING("CANNOT STOP AUDIO - RESTART CONSOLE", status.error);
        TEST_ASSERT_FALSE(audio.active);
        TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
        TEST_ASSERT_EQUAL_INT(1, network_opened);
        TEST_ASSERT_EQUAL_INT(0, output_closed);
    }

    TEST_ASSERT_EQUAL_INT(4, network_closed);

    network_close_pending = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, status.kind);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_STRING("", status.error);
    TEST_ASSERT_EQUAL_INT(OUTPUT_CD, sound_owner);
}

/**
 * @brief Verify prolonged output cleanup retains resources.
 */
static void prolonged_output_cleanup_retains_resources(void)
{
    Audio             audio;
    AudioSourceStatus status;
    output_close_ok = 0;

    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR_OUTPUT_CLOSE, audio_source_close());

    clock_ms += 5000;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_STRING("CANNOT STOP AUDIO - RESTART CONSOLE", status.error);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR_OUTPUT_CLOSE, audio_source_open(1, 0));
    TEST_ASSERT_EQUAL_INT(1, output_opens);
    TEST_ASSERT_EQUAL_INT(1, cd_opened);

    output_close_ok = 1;

    TEST_ASSERT_EQUAL_INT(0, audio_source_open(1, 0));
    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_EQUAL_STRING("", status.error);
}

/**
 * @brief Verify output cleanup failure blocks reopen.
 */
static void output_cleanup_failure_blocks_reopen(void)
{
    Audio             audio;
    AudioSourceStatus status;
    output_close_ok = 0;

    TEST_ASSERT_TRUE(!(audio_source_close() == 0));
    TEST_ASSERT_EQUAL_INT(1, output_closed);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR_OUTPUT_CLOSE, audio_source_open(0, 1));
    TEST_ASSERT_EQUAL_INT(2, output_closed);
    TEST_ASSERT_EQUAL_INT(1, cd_opened);
    TEST_ASSERT_EQUAL_INT(1, configured_autoplay);
    TEST_ASSERT_EQUAL_INT(0, configured_mute);
    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_INT(1, cd_opened);

    output_close_ok = 1;

    audio_source_open(0, 1);
    TEST_ASSERT_EQUAL_INT(3, output_closed);
    TEST_ASSERT_EQUAL_INT(2, cd_opened);
    TEST_ASSERT_EQUAL_INT(0, configured_autoplay);
    TEST_ASSERT_EQUAL_INT(1, configured_mute);
}

/** @brief Failed startup retries retained cleanup before allocating another worker. */
static void failed_cd_start_waits_for_cleanup_and_retry_interval(void)
{
    TEST_ASSERT_TRUE(audio_source_close() == 0);

    cd_open_ok = 0;
    clock_ms   = UINT32_MAX - 500;

    audio_source_open(1, 0);
    TEST_ASSERT_EQUAL_INT(2, cd_opened);

    Audio             audio;
    AudioSourceStatus status;
    cd_close_ok = 0;
    clock_ms += 2000;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("CD WORKER START FAILED", status.error);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
    TEST_ASSERT_EQUAL_INT(2, cd_opened);
    TEST_ASSERT_EQUAL_INT(0, network_opened);

    cd_close_ok = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_TRUE(status.network_ready);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
    TEST_ASSERT_EQUAL_INT(3, cd_opened);

    cd_open_ok    = 1;
    disc.checking = 1;
    clock_ms += 999;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_EQUAL_INT(3, cd_opened);

    ++clock_ms;
    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_EQUAL_INT(4, cd_opened);
    TEST_ASSERT_EQUAL_INT(1, configured_autoplay);
    TEST_ASSERT_EQUAL_INT(0, configured_mute);
}

/** @brief Failed network startup retries cleanup before a bounded restart. */
static void failed_network_start_waits_for_cleanup_and_retry_interval(void)
{
    Audio             audio;
    AudioSourceStatus status;

    network_ok = 0;
    clock_ms   = UINT32_MAX - 500;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("NETWORK ERROR", status.error);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
    TEST_ASSERT_EQUAL_INT(0, network_polls);

    network_close_ok = 0;
    clock_ms += 2000;

    for (unsigned i = 0; i < 2; ++i)
    {
        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
        TEST_ASSERT_EQUAL_STRING("NETWORK ERROR", status.error);
        TEST_ASSERT_FALSE(audio.active);
        TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
        TEST_ASSERT_EQUAL_INT(1, network_opened);
        TEST_ASSERT_EQUAL_INT(0, network_polls);
        TEST_ASSERT_EQUAL_INT(i + 1, network_closed);
    }

    network_close_ok = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(3, network_closed);
    TEST_ASSERT_EQUAL_INT(2, network_opened);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("NETWORK ERROR", status.error);

    network_ok = 1;
    clock_ms += 999;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(4, network_closed);
    TEST_ASSERT_EQUAL_INT(2, network_opened);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("NETWORK ERROR", status.error);

    ++clock_ms;
    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(3, network_opened);
    TEST_ASSERT_EQUAL_INT(1, network_polls);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_TRUE(status.network_ready);
}

/** @brief Shared output lock failure disables producers until console restart. */
static void output_restart_required_case(void)
{
    TEST_ASSERT_EQUAL_INT(0, audio_source_close());

    output_open_ok = 0;

    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR_OUTPUT_RESTART_REQUIRED, audio_source_open(0, 1));
    TEST_ASSERT_EQUAL_INT(2, output_opens);

    disc.playing = 1;
    disc.present = 1;

    Audio             audio;
    AudioSourceStatus status;

    for (unsigned i = 0; i < 3; ++i)
    {
        clock_ms += 2000;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
        TEST_ASSERT_EQUAL_STRING("SOUND OUTPUT UNAVAILABLE - RESTART REQUIRED", status.error);
        TEST_ASSERT_FALSE(audio.active);
        TEST_ASSERT_FALSE(status.cd.present);
        TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
        TEST_ASSERT_EQUAL_INT(1, cd_opened);
        TEST_ASSERT_EQUAL_INT(0, network_opened);
        TEST_ASSERT_EQUAL_INT(2, output_opens);
    }

    output_open_ok = 1;

    TEST_ASSERT_EQUAL_INT(0, audio_source_close());
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR_OUTPUT_RESTART_REQUIRED, audio_source_open(0, 1));
    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("SOUND OUTPUT UNAVAILABLE - RESTART REQUIRED", status.error);
    TEST_ASSERT_EQUAL_INT(2, output_opens);
    TEST_ASSERT_EQUAL_INT(1, cd_opened);
    TEST_ASSERT_EQUAL_INT(0, network_opened);
    TEST_ASSERT_EQUAL_INT(1, configured_autoplay);
    TEST_ASSERT_EQUAL_INT(0, configured_mute);
}

/**
 * @brief Verify output start failure requires restart.
 */
static void output_start_failure_requires_restart(void)
{
    test_process_run(output_restart_required_case);
}

/** @brief A fatal poll failure releases stale state and restarts only after the retry interval. */
static void runtime_network_failure_restarts_after_delay(void)
{
    Audio             audio;
    AudioSourceStatus status;

    listening = 1;
    active    = 1;
    clock_ms  = UINT32_MAX - 500;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_STRING("Network track", status.metadata.title);

    network_ok = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("NETWORK ERROR", status.error);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_FALSE(status.listening);
    TEST_ASSERT_EQUAL_UINT(0, status.network_generation);
    TEST_ASSERT_EQUAL_STRING("", status.metadata.title);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);

    network_ok = 1;
    listening  = 0;
    active     = 0;
    clock_ms += 999;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("NETWORK ERROR", status.error);
    TEST_ASSERT_EQUAL_INT(1, network_closed);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
    TEST_ASSERT_EQUAL_INT(2, network_polls);

    ++clock_ms;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(2, network_opened);
    TEST_ASSERT_TRUE(status.network_ready);
    TEST_ASSERT_EQUAL_STRING("", status.error);
    TEST_ASSERT_TRUE(status.network_waiting);

    active = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NETWORK, sound_owner);
}

/** @brief Snapshot failure retains cleanup ownership and cannot reuse a worker before its join succeeds. */
static void runtime_snapshot_failure_waits_for_cleanup(void)
{
    Audio             audio;
    AudioSourceStatus status;

    active = 1;

    audio_source_poll(&audio, &status);

    fail_on_copy = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("NETWORK RECEIVER FAILED", status.error);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);

    network_close_ok = 0;
    clock_ms += 2000;

    for (unsigned i = 0; i < 2; ++i)
    {
        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
        TEST_ASSERT_EQUAL_STRING("NETWORK RECEIVER FAILED", status.error);
        TEST_ASSERT_EQUAL_INT(1, network_opened);
        TEST_ASSERT_EQUAL_INT(2, network_polls);
        TEST_ASSERT_EQUAL_INT(i + 1, network_closed);
    }

    network_close_ok = 1;
    fail_on_copy     = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(3, network_closed);
    TEST_ASSERT_EQUAL_INT(2, network_opened);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NETWORK, sound_owner);
}

/** @brief Once failed receiver cleanup finishes, an inserted CD can take ownership. */
static void runtime_network_failure_allows_cd_selection(void)
{
    Audio             audio;
    AudioSourceStatus status;

    audio_source_poll(&audio, &status);

    network_ok = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);

    disc.playing = 1;
    disc.present = 1;
    clock_ms += 1000;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(1, network_closed);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, status.kind);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_CD, sound_owner);
}

/**
 * @brief Verify failed drive start retries cleanup before detection.
 */
static void failed_drive_start_retries_cleanup_before_detection(void)
{
    Audio             audio;
    AudioSourceStatus status;

    cd_close_ok  = 0;
    cd_available = 0;

    strcpy(disc.error, "CD DRIVE INIT FAILED");
    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("CD DRIVE INIT FAILED", status.error);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
    TEST_ASSERT_EQUAL_INT(0, network_opened);

    disc.error[0] = 0;
    cd_close_ok   = 0;
    clock_ms      = 100;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_INT(1, cd_opened);
    TEST_ASSERT_EQUAL_STRING("CD DRIVE INIT FAILED", status.error);

    cd_close_ok = 1;
    clock_ms    = 999;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_TRUE(status.network_ready);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
    TEST_ASSERT_EQUAL_INT(1, cd_opened);

    cd_available = 1;
    disc         = (CdPlaybackStatus){ .present = 1, .playing = 1, .tracks = 2 };
    clock_ms     = 1000;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(2, cd_opened);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, status.kind);
    TEST_ASSERT_EQUAL_STRING("", status.error);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_CD, sound_owner);
    TEST_ASSERT_EQUAL_INT(1, configured_autoplay);
    TEST_ASSERT_EQUAL_INT(0, configured_mute);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
    TEST_ASSERT_EQUAL_INT(1, network_closed);
}

/** @brief Persistent CD startup failures leave an idle receiver available and defer retries during a stream. */
static void check_cd_failure_network_fallback(int silent)
{
    Audio             audio;
    AudioSourceStatus status;
    int               initial_cd_starts = cd_opened;

    for (unsigned i = 0; i < 12; ++i)
    {
        clock_ms = i * 1000;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
        TEST_ASSERT_TRUE(status.network_ready);
        TEST_ASSERT_TRUE(status.network_waiting);
        TEST_ASSERT_FALSE(status.cd.present);
        TEST_ASSERT_FALSE(status.cd.checking);
        TEST_ASSERT_EQUAL_STRING("", status.cd.error);
        TEST_ASSERT_EQUAL_INT(1, network_opened);
        TEST_ASSERT_EQUAL_INT(0, network_closed);
        TEST_ASSERT_EQUAL_INT(initial_cd_starts + (int)i, cd_opened);
        TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);

        clock_ms += 999;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
        TEST_ASSERT_TRUE(status.network_waiting);
        TEST_ASSERT_EQUAL_INT(initial_cd_starts + (int)i, cd_opened);
    }

    active    = 1;
    listening = silent;
    clock_ms += 2000;

    for (unsigned i = 0; i < 3; ++i)
    {
        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
        TEST_ASSERT_TRUE(status.network_ready);
        TEST_ASSERT_FALSE(status.network_waiting);
        TEST_ASSERT_EQUAL_INT(silent, status.listening);
        TEST_ASSERT_TRUE(audio.active);
        TEST_ASSERT_EQUAL_STRING("Network track", status.metadata.title);
        TEST_ASSERT_EQUAL_INT(silent ? OUTPUT_NONE : OUTPUT_NETWORK, sound_owner);
        TEST_ASSERT_EQUAL_INT(initial_cd_starts + 11, cd_opened);
        TEST_ASSERT_EQUAL_INT(1, network_opened);
        TEST_ASSERT_EQUAL_INT(0, network_closed);

        clock_ms += 1000;
    }

    listening = 0;
    active    = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
    TEST_ASSERT_TRUE(status.network_waiting);
    TEST_ASSERT_FALSE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
    TEST_ASSERT_EQUAL_INT(initial_cd_starts + 12, cd_opened);
}

/**
 * @brief Verify failed cd start allows network playback.
 */
static void failed_cd_start_allows_network_playback(void)
{
    TEST_ASSERT_TRUE(audio_source_close() == 0);

    cd_open_ok = 0;

    audio_source_open(1, 0);
    check_cd_failure_network_fallback(0);
}

/**
 * @brief Verify failed drive start allows network listening.
 */
static void failed_drive_start_allows_network_listening(void)
{
    cd_available = 0;

    strcpy(disc.error, "CD DRIVE INIT FAILED");
    check_cd_failure_network_fallback(1);
}

/**
 * @brief Exercise source coordination after a restart-required CD failure.
 */
static void cd_restart_required_case(void)
{
    Audio             audio;
    AudioSourceStatus status;

    disc.restart_required = 1;

    strcpy(disc.error, "CD DRIVE UNRESPONSIVE - RESTART REQUIRED");

    cd_close_ok = 0;

    for (unsigned i = 0; i < 3; ++i)
    {
        clock_ms += 2000;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
        TEST_ASSERT_EQUAL_STRING("CD DRIVE UNRESPONSIVE - RESTART REQUIRED", status.error);
        TEST_ASSERT_EQUAL_INT(1, cd_opened);
        TEST_ASSERT_EQUAL_INT(0, network_opened);
        TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
    }

    cd_close_pending = 1;
    cd_close_ok      = 1;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_STRING("CD DRIVE UNRESPONSIVE - RESTART REQUIRED", status.error);
    TEST_ASSERT_EQUAL_INT(0, network_opened);

    cd_close_pending = 0;

    memset(&disc, 0, sizeof(disc)); /* Backend cleanup can erase the cause. */

    for (unsigned i = 0; i < 3; ++i)
    {
        clock_ms += 2000;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
        TEST_ASSERT_TRUE(status.network_ready);
        TEST_ASSERT_EQUAL_INT(1, cd_opened);
    }

    active = 1;

    for (listening = 0; listening < 2; ++listening)
    {
        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
        TEST_ASSERT_EQUAL_INT(listening ? OUTPUT_NONE : OUTPUT_NETWORK, sound_owner);
        TEST_ASSERT_EQUAL_INT(1, cd_opened);
    }

    listening = 0;
    active    = 0;

    TEST_ASSERT_EQUAL_INT(0, audio_source_close());
    audio_source_open(1, 0);

    for (unsigned i = 0; i < 3; ++i)
    {
        clock_ms += 2000;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_NETWORK, status.kind);
        TEST_ASSERT_TRUE(status.network_ready);
        TEST_ASSERT_EQUAL_INT(1, cd_opened);
    }
}

/**
 * @brief Verify cd restart required stops retries and allows network.
 */
static void cd_restart_required_stops_retries_and_allows_network(void)
{
    /* The terminal flag lasts for the process lifetime. Isolate this case. */
    test_process_run(cd_restart_required_case);
}

/**
 * @brief Verify restart required stops retries and allows cd.
 */
static void restart_required_stops_retries_and_allows_cd(void)
{
    Audio             audio;
    AudioSourceStatus status;

    network_restart_required = 1;
    network_close_ok         = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(1, network_opened);

    for (unsigned i = 0; i < 3; ++i)
    {
        clock_ms += 2000;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
        TEST_ASSERT_EQUAL_STRING("RESTART CONSOLE TO LOAD NETWORK MODULE", status.error);
        TEST_ASSERT_EQUAL_INT(1, network_opened);
        TEST_ASSERT_FALSE(audio.active);
        TEST_ASSERT_EQUAL_INT(OUTPUT_NONE, sound_owner);
    }

    network_close_ok         = 1;
    network_restart_required = 0; /* Backend text can disappear after cleanup. */

    for (unsigned i = 0; i < 3; ++i)
    {
        clock_ms += 2000;

        audio_source_poll(&audio, &status);
        TEST_ASSERT_EQUAL_STRING("RESTART CONSOLE TO LOAD NETWORK MODULE", status.error);
        TEST_ASSERT_EQUAL_INT(1, network_opened);
    }

    TEST_ASSERT_EQUAL_INT(0, audio_source_close());
    audio_source_open(1, 0);

    disc = (CdPlaybackStatus){ .present = 1, .playing = 1, .tracks = 2 };

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_CD, status.kind);
    TEST_ASSERT_EQUAL_STRING("", status.error);
    TEST_ASSERT_TRUE(audio.active);
    TEST_ASSERT_EQUAL_INT(OUTPUT_CD, sound_owner);

    disc.playing = 0;
    disc.present = 0;

    audio_source_poll(&audio, &status);
    TEST_ASSERT_EQUAL_INT(AUDIO_SOURCE_ERROR, status.kind);
    TEST_ASSERT_EQUAL_STRING("RESTART CONSOLE TO LOAD NETWORK MODULE", status.error);
    TEST_ASSERT_EQUAL_INT(1, network_opened);
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(streaming_receiver_starts_only_after_first_poll);
    RUN_TEST(listening_receiver_starts_only_after_first_poll);
    RUN_TEST(transport_routes_only_to_selected_source);
    RUN_TEST(startup_audio_preferences);
    RUN_TEST(tray_close_on_waiting_screen_selects_cd);
    RUN_TEST(active_stream_defers_disc_until_disconnect);
    RUN_TEST(startup_cd_keeps_network_inactive);
    RUN_TEST(no_cd_selects_network_once);
    RUN_TEST(listening_analyzes_without_output_ownership);
    RUN_TEST(snapshots_handle_disc_and_session_changes);
    RUN_TEST(unavailable_sources_report_network_failure);
    RUN_TEST(cd_join_failure_defers_selection);
    RUN_TEST(pending_cd_cleanup_is_waiting);
    RUN_TEST(pending_network_cleanup_is_waiting);
    RUN_TEST(network_join_failure_defers_selection);
    RUN_TEST(failed_shutdown_preserves_ownership);
    RUN_TEST(prolonged_cd_cleanup_retains_resources);
    RUN_TEST(prolonged_network_cleanup_retains_resources);
    RUN_TEST(prolonged_output_cleanup_retains_resources);
    RUN_TEST(output_cleanup_failure_blocks_reopen);
    RUN_TEST(output_start_failure_requires_restart);
    RUN_TEST(failed_cd_start_waits_for_cleanup_and_retry_interval);
    RUN_TEST(failed_drive_start_retries_cleanup_before_detection);
    RUN_TEST(failed_cd_start_allows_network_playback);
    RUN_TEST(failed_drive_start_allows_network_listening);
    RUN_TEST(failed_network_start_waits_for_cleanup_and_retry_interval);
    RUN_TEST(runtime_network_failure_restarts_after_delay);
    RUN_TEST(runtime_snapshot_failure_waits_for_cleanup);
    RUN_TEST(runtime_network_failure_allows_cd_selection);
    RUN_TEST(cd_restart_required_stops_retries_and_allows_network);
    /* Terminal startup state intentionally lasts for the process lifetime. */
    RUN_TEST(restart_required_stops_retries_and_allows_cd);

    return UNITY_END();
}
