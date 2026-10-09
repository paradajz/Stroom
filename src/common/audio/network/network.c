#include "util/diagnostics.h"
#include "audio/network/ariacast/server.h"
#include "audio/network/network.h"
#include "audio/artwork/receiver.h"
#include "audio/network/ariacast/playback.h"
#include "audio/common/status.h"
#include "audio/common/pcm.h"
#include "platform/network/runtime.h"
#include "platform/network/socket_mode.h"
#include "audio/common/worker_config.h"
#include "platform/time/clock.h"
#include "platform/time/sleep.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "platform/thread/worker.h"

#define ARTWORK_PENDING_OBSERVATIONS  32
#define NETWORK_ADDRESS_REFRESH_MS    2000
#define NETWORK_DIAGNOSTIC_ARTWORK_MS 50
#define NETWORK_STALL_MS              2000

static int      receive_failed;
static char     aria_error[AUDIO_STATUS_TEXT_BYTES];
static char     incoming_address[16], address_text[16];
static uint32_t address_checked;
static char     incoming_error[AUDIO_STATUS_TEXT_BYTES];

static char error_text[AUDIO_STATUS_TEXT_BYTES];

static int               initialized;
static int               startup_succeeded;
static Ps2Worker         background = PS2_WORKER_INITIALIZER;
static volatile uint32_t worker_progress;
static const char* volatile worker_operation;
static unsigned char   worker_stack[AUDIO_WORKER_STACK_BYTES] __attribute__((aligned(16)));
static AudioBuffer     incoming;
static AriaServer      aria;
static AriaPlayback    aria_playback;
static int             aria_opened;
static int             aria_receiving;
static int             incoming_listening, listening;
static char            incoming_device_name[METADATA_DEVICE_NAME_BYTES], device_name[METADATA_DEVICE_NAME_BYTES];
static unsigned        incoming_generation;
static TrackMetadata   incoming_metadata;
static ArtworkReceiver artwork_receiver;
static ArtworkBlob     incoming_artwork;
#if STROOM_DIAGNOSTICS
static char incoming_artwork_display[32];

/** UI/decoder observation mailbox; only the audio worker writes the diagnostic recorder. */
typedef struct
{
    AriaDiagnosticCaptureRecord record;
    unsigned                    generation;
} PendingArtworkObservation;

static PendingArtworkObservation artwork_pending[ARTWORK_PENDING_OBSERVATIONS];
static unsigned                  artwork_pending_count, artwork_pending_lost;
static unsigned                  incoming_metadata_revision;

/** @brief Encode one observation without querying sound or sockets. */
static AriaDiagnosticCaptureRecord artwork_record(ArtworkObservation observation, unsigned revision)
{
    return (AriaDiagnosticCaptureRecord){ .at = observation.end, .kind = DIAGNOSTIC_CAPTURE_COVER, .data = { [DIAGNOSTIC_CAPTURE_COVER_PHASE] = observation.phase, [DIAGNOSTIC_CAPTURE_COVER_DURATION_MS] = observation.end - observation.begin, [DIAGNOSTIC_CAPTURE_COVER_METADATA_REVISION] = revision, [DIAGNOSTIC_CAPTURE_COVER_VALUE0] = observation.values[0], [DIAGNOSTIC_CAPTURE_COVER_VALUE1] = observation.values[1], [DIAGNOSTIC_CAPTURE_COVER_VALUE2] = observation.values[2] } };
}

/** @brief Keep worker-owned history and current-cover summary together. */
static void artwork_retain(AriaDiagnosticCaptureRecord record)
{
    aria_diagnostic_capture_record(&aria.diagnostics.diagnostic_capture, record);

    if (record.data[2] == aria.metadata_revision)
    {
        aria_diagnostics_cover(&aria.diagnostics, record);
    }
}

/** @brief Record a worker-owned fetch operation directly, retaining stale-close identity as zero. */
static void artwork_worker_observe(const TrackMetadata* metadata, ArtworkObservation observation)
{
    unsigned revision = track_metadata_equal(metadata, &aria.metadata) ? aria.metadata_revision : 0;

    artwork_retain(artwork_record(observation, revision));
}

/** @brief Drain the observation mailbox outside the snapshot lock, rejecting previous sessions. */
static void artwork_observations_drain(void)
{
    PendingArtworkObservation pending[ARTWORK_PENDING_OBSERVATIONS];

    platform_worker_lock(&background);

    unsigned count = artwork_pending_count, lost = artwork_pending_lost;

    memcpy(pending, artwork_pending, count * sizeof(*pending));

    artwork_pending_count = artwork_pending_lost = 0;

    platform_worker_unlock(&background);

    for (unsigned i = 0; i < count; ++i)
    {
        if (pending[i].generation == aria.stream.generation)
        {
            artwork_retain(pending[i].record);
        }
        else
        {
            ++lost;
        }
    }

    if (lost)
    {
        ArtworkObservation observation = { .phase = DIAGNOSTIC_ARTWORK_OBSERVATIONS_LOST, .begin = platform_millis(), .values = { lost, 0, 0 } };

        observation.end = observation.begin;

        artwork_retain(artwork_record(observation, aria.metadata_revision));
    }
}
#endif
int network_listening(void)
{
    return listening;
}

const char* network_device_name(void)
{
    return device_name;
}

const char* network_address(void)
{
    return address_text;
}

/**
 * @brief Query the network address and copy it to shared state under the audio lock.
 *
 * @param now Current monotonic time in milliseconds.
 */
static void refresh_address(uint32_t now)
{
    char address[16] = { 0 };

    platform_network_address(address, sizeof(address));

    if (background.lock >= 0)
    {
        platform_worker_lock(&background);
    }

    memcpy(incoming_address, address, sizeof(address));

    if (background.lock >= 0)
    {
        platform_worker_unlock(&background);
    }

    address_checked = now;
}

const char* network_error(void)
{
    return error_text;
}

/**
 * @brief Record a network startup failure and release partially initialized resources.
 *
 * @param stage Failed startup stage.
 * @param code Error code.
 * @return Negative failure for propagation to the caller.
 */
static int fail(const char* stage, int code)
{
    snprintf(error_text, sizeof(error_text), "%s %d", stage, code);
    STROOM_LOG("network startup failed: %s", error_text);
    network_close();

    return NETWORK_ERROR_WORKER_START;
}

/**
 * @brief Publish session identity and labels while holding the audio lock.
 * @param now Current monotonic milliseconds.
 */
static void publish_session(uint32_t now)
{
    if (incoming_generation != aria.stream.generation)
    {
        memset(&incoming, 0, sizeof(incoming));
    }

#if STROOM_DIAGNOSTICS
    if (incoming_generation != aria.stream.generation || !track_metadata_equal(&incoming_metadata, &aria.metadata))
    {
        incoming_artwork_display[0] = 0;
    }
#endif
    incoming_generation = aria.stream.generation;
#if STROOM_DIAGNOSTICS
    incoming_metadata_revision = aria.metadata_revision;
#endif
    incoming_metadata  = aria.metadata;
    aria_receiving     = aria_server_active(&aria, now);
    incoming_listening = aria.stream.listening;

    memcpy(incoming_device_name, aria.stream.device_name, sizeof(incoming_device_name));
}

/**
 * @brief Publish PCM consumed by sound output or the listening clock.
 * @param context Unused.
 * @param pcm Audio samples, or null to clear.
 * @param frames Stereo frame count.
 */
static void capture_stream(void* context, const uint8_t* pcm, unsigned frames)
{
    (void)context;
#if STROOM_DIAGNOSTICS
    uint32_t begin = platform_millis();
#endif
    platform_worker_lock(&background);
#if STROOM_DIAGNOSTICS
    uint32_t acquired = platform_millis();
#endif

    publish_session(platform_millis());

    if (frames)
    {
        audio_push_pcm(&incoming, pcm, frames, platform_millis());
    }
    else
    {
        memset(&incoming, 0, sizeof(incoming));
    }

    platform_worker_unlock(&background);
#if STROOM_DIAGNOSTICS
    uint32_t end = platform_millis();

    if ((uint32_t)(end - begin) >= DIAGNOSTIC_CAPTURE_SLOW_MS)
    {
        AriaDiagnosticCaptureRecord record = { .at = end, .kind = DIAGNOSTIC_CAPTURE_STAGE, .data = { [DIAGNOSTIC_CAPTURE_STAGE_STAGE] = 4, [DIAGNOSTIC_CAPTURE_STAGE_DURATION_MS] = end - begin, [DIAGNOSTIC_CAPTURE_STAGE_POLL_GAP_MS] = acquired - begin, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED1] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED2] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED3] = 0 } };

        aria_diagnostic_capture_record(&aria.diagnostics.diagnostic_capture, record);
    }
#endif
}

/**
 * @brief Acquire the worker's shared snapshot lock and retain unexpectedly long waits.
 */
static void worker_audio_lock(void)
{
#if STROOM_DIAGNOSTICS
    uint32_t begin = platform_millis();
#endif
    platform_worker_lock(&background);
#if STROOM_DIAGNOSTICS
    uint32_t end = platform_millis();

    if ((uint32_t)(end - begin) >= DIAGNOSTIC_CAPTURE_SLOW_MS)
    {
        AriaDiagnosticCaptureRecord record = { .at = end, .kind = DIAGNOSTIC_CAPTURE_STAGE, .data = { [DIAGNOSTIC_CAPTURE_STAGE_STAGE] = DIAGNOSTIC_CAPTURE_STAGE_LOCK, [DIAGNOSTIC_CAPTURE_STAGE_DURATION_MS] = end - begin, [DIAGNOSTIC_CAPTURE_STAGE_POLL_GAP_MS] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED1] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED2] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED3] = 0 } };

        aria_diagnostic_capture_record(&aria.diagnostics.diagnostic_capture, record);
    }
#endif
}

/**
 * @brief Service network audio and sound output above renderer priority.
 *
 * @param arg Unused thread argument.
 */
static void receive_worker(void* arg)
{
    (void)arg;
#if STROOM_DIAGNOSTICS
    uint32_t previous_end          = 0;
    uint32_t artwork_diagnostic_at = 0;
#endif

    while (background.running)
    {
        uint32_t now = platform_millis();

#if STROOM_DIAGNOSTICS
        if (previous_end && (uint32_t)(now - previous_end) >= DIAGNOSTIC_CAPTURE_SLOW_MS)
        {
            AriaDiagnosticCaptureRecord record = { .at = now, .kind = DIAGNOSTIC_CAPTURE_STAGE, .data = { [DIAGNOSTIC_CAPTURE_STAGE_STAGE] = DIAGNOSTIC_CAPTURE_STAGE_SLEEP, [DIAGNOSTIC_CAPTURE_STAGE_DURATION_MS] = now - previous_end, [DIAGNOSTIC_CAPTURE_STAGE_POLL_GAP_MS] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED1] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED2] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED3] = 0 } };

            aria_diagnostic_capture_record(&aria.diagnostics.diagnostic_capture, record);
        }
#endif
        worker_progress  = now;
        worker_operation = "SOCKETS";

        aria_server_step(&aria, now, incoming_address, !output_selected(OUTPUT_CD));
#if STROOM_DIAGNOSTICS
        uint32_t socket_ms = platform_millis() - now;

        if (socket_ms >= DIAGNOSTIC_CAPTURE_SLOW_MS)
        {
            AriaDiagnosticCaptureRecord record = { .at = platform_millis(), .kind = DIAGNOSTIC_CAPTURE_STAGE, .data = { [DIAGNOSTIC_CAPTURE_STAGE_STAGE] = 1, [DIAGNOSTIC_CAPTURE_STAGE_DURATION_MS] = socket_ms, [DIAGNOSTIC_CAPTURE_STAGE_POLL_GAP_MS] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED1] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED2] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED3] = 0 } };

            aria_diagnostic_capture_record(&aria.diagnostics.diagnostic_capture, record);
        }

        if (socket_ms > aria.diagnostics.max_socket_ms)
        {
            aria.diagnostics.max_socket_ms = socket_ms;
        }
#endif

#if STROOM_DIAGNOSTICS
        artwork_observations_drain();
#endif

        if (aria.listener < 0)
        {
            snprintf(aria_error, sizeof(aria_error), "%s", aria.error[0] ? aria.error : "ARIACAST SOCKET ERROR");
            worker_audio_lock();

            receive_failed = 1;

            platform_worker_unlock(&background);

            background.running = 0;
        }

        worker_audio_lock();
        publish_session(now);
        platform_worker_unlock(&background);

        OutputRuntime output = { .running = &background.running, .error = aria_error, .capacity = sizeof(aria_error) };

        worker_operation = "SOUND OUTPUT";

        uint32_t playback_at = platform_millis();

        aria_playback_step(&aria_playback, &aria.stream, ARIA_DIAGNOSTICS(&aria), &output, playback_at, capture_stream, NULL);
#if STROOM_DIAGNOSTICS
        uint32_t playback_ms = platform_millis() - playback_at;

        if (playback_ms >= DIAGNOSTIC_CAPTURE_SLOW_MS)
        {
            AriaDiagnosticCaptureRecord record = { .at = platform_millis(), .kind = DIAGNOSTIC_CAPTURE_STAGE, .data = { [DIAGNOSTIC_CAPTURE_STAGE_STAGE] = 2, [DIAGNOSTIC_CAPTURE_STAGE_DURATION_MS] = playback_ms, [DIAGNOSTIC_CAPTURE_STAGE_POLL_GAP_MS] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED1] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED2] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED3] = 0 } };

            aria_diagnostic_capture_record(&aria.diagnostics.diagnostic_capture, record);
        }

        if (playback_ms > aria.diagnostics.max_playback_ms)
        {
            aria.diagnostics.max_playback_ms = playback_ms;
        }
#endif
        worker_audio_lock();
        publish_session(platform_millis());
#if STROOM_DIAGNOSTICS
        snprintf(aria.diagnostics.artwork_display, sizeof(aria.diagnostics.artwork_display), "%s", incoming_artwork_display);
#endif

        if (aria_error[0])
        {
            snprintf(incoming_error, sizeof(incoming_error), "%s", aria_error);

            aria_error[0] = 0;
        }
        else if (aria.error[0])
        {
            snprintf(incoming_error, sizeof(incoming_error), "%s", aria.error);
        }
        else if (aria_receiving)
        {
            incoming_error[0] = 0;
        }

        platform_worker_unlock(&background);

        worker_operation = "ARTWORK";

        const TrackMetadata  empty_artwork    = { 0 };
        const TrackMetadata* artwork_metadata = aria_receiving && !aria.stream.listening ? &aria.metadata : &empty_artwork;
        uint32_t             artwork_at       = platform_millis();
#if STROOM_DIAGNOSTICS
        artwork_receiver.observer = artwork_worker_observe;
#endif
        artwork_receiver_step(&artwork_receiver, artwork_metadata, ARTWORK_PER_TRACK, artwork_at, platform_socket_nonblocking, 1);
#if STROOM_DIAGNOSTICS
        uint32_t artwork_ms = platform_millis() - artwork_at;

        if (artwork_ms >= DIAGNOSTIC_CAPTURE_SLOW_MS)
        {
            AriaDiagnosticCaptureRecord record = { .at = platform_millis(), .kind = DIAGNOSTIC_CAPTURE_STAGE, .data = { [DIAGNOSTIC_CAPTURE_STAGE_STAGE] = 3, [DIAGNOSTIC_CAPTURE_STAGE_DURATION_MS] = artwork_ms, [DIAGNOSTIC_CAPTURE_STAGE_POLL_GAP_MS] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED1] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED2] = 0, [DIAGNOSTIC_CAPTURE_STAGE_UNUSED3] = 0 } };

            aria_diagnostic_capture_record(&aria.diagnostics.diagnostic_capture, record);
        }

        if (aria.audio_fd >= 0 && (uint32_t)(artwork_at - artwork_diagnostic_at) >= NETWORK_DIAGNOSTIC_ARTWORK_MS)
        {
            artwork_diagnostic_at = artwork_at;

            AriaDiagnosticCaptureRecord record = { .at = platform_millis(), .kind = DIAGNOSTIC_CAPTURE_ARTWORK, .data = { [DIAGNOSTIC_CAPTURE_ARTWORK_SOCKET_OPEN] = artwork_receiver.fd >= 0, [DIAGNOSTIC_CAPTURE_ARTWORK_DOWNLOADED_BYTES] = artwork_receiver.http.used, [DIAGNOSTIC_CAPTURE_ARTWORK_COMPLETE] = (unsigned)artwork_receiver.complete, [DIAGNOSTIC_CAPTURE_ARTWORK_ATTEMPT] = artwork_receiver.attempts, [DIAGNOSTIC_CAPTURE_ARTWORK_ERROR] = (uint32_t)artwork_receiver.error, [DIAGNOSTIC_CAPTURE_ARTWORK_METADATA_AT] = aria.metadata_at } };

            aria_diagnostic_capture_record(&aria.diagnostics.diagnostic_capture, record);
        }

        if (artwork_ms > aria.diagnostics.max_artwork_ms)
        {
            aria.diagnostics.max_artwork_ms = artwork_ms;
        }

        if (artwork_ms > artwork_receiver.max_step_ms)
        {
            artwork_receiver.max_step_ms = artwork_ms;
        }
#endif
        ArtworkBlob completed     = { 0 };
        int         artwork_ready = artwork_receiver_take(&artwork_receiver, &completed) == 0;
#if STROOM_DIAGNOSTICS
        snprintf(aria.diagnostics.artwork_fetch, sizeof(aria.diagnostics.artwork_fetch), "%s; FAILURE %s; ERR %d; HTTP %d; REQUEST %u/%u; RESPONSE %u; IMAGE %u; ATTEMPT %u; MAX CALL %u MS", artwork_receiver.stage ? artwork_receiver.stage : "IDLE", artwork_receiver.failure ? artwork_receiver.failure : "NONE", artwork_receiver.error, artwork_receiver.http.status_code, artwork_receiver.sent, artwork_receiver.request_size, artwork_receiver.received, artwork_receiver.http.used, artwork_receiver.attempts, (unsigned)artwork_receiver.max_step_ms);
#endif
        worker_audio_lock();

        if (artwork_ready || !track_metadata_equal(&incoming_artwork.metadata, artwork_metadata))
        {
            free(incoming_artwork.data);

            incoming_artwork = completed;
        }

        platform_worker_unlock(&background);

        /* Playback and artwork RPCs can advance the clock beyond the loop start.
         * Compare freshly captured PCM against a current timestamp. */
        uint32_t address_at = platform_millis();

        if ((uint32_t)(address_at - address_checked) >= NETWORK_ADDRESS_REFRESH_MS && (uint32_t)(address_at - incoming.last_ms) >= AUDIO_STALE_MS)
        {
            worker_operation = "ADDRESS QUERY";

            refresh_address(address_at);
        }

        worker_operation = "SLEEP";
#if STROOM_DIAGNOSTICS
        previous_end = platform_millis();
#endif
        platform_sleep_us(AUDIO_WORKER_IDLE_US);
    }

    aria_playback_stop(&aria_playback);
    platform_worker_finish(&background);
}

/**
 * @brief Create synchronization resources and launch the prioritized receiver.
 *
 * @return 0 on success; negative after recording and cleaning up a failure.
 */
static int start_worker(void)
{
    worker_progress  = platform_millis();
    worker_operation = "STARTING";

    const Ps2WorkerConfig config = { .entry = receive_worker, .stack = worker_stack, .stack_bytes = sizeof(worker_stack), .priority = AUDIO_WORKER_PRIORITY };
    Ps2WorkerError        error;

    if (platform_worker_open(&background, &config, &error) != 0)
    {
        return fail(error.stage, error.code);
    }

    return 0;
}

int network_open(void)
{
    int closed = network_close();

    if (closed != 0)
    {
        return closed;
    }

    memset(&incoming, 0, sizeof(incoming));
    memset(&incoming_metadata, 0, sizeof(incoming_metadata));
    memset(incoming_address, 0, sizeof(incoming_address));
    memset(address_text, 0, sizeof(address_text));
    memset(incoming_error, 0, sizeof(incoming_error));
    memset(error_text, 0, sizeof(error_text));

    address_checked     = 0;
    incoming_generation = 0;
#if STROOM_DIAGNOSTICS
    artwork_pending_count = artwork_pending_lost = incoming_metadata_revision = 0;
#endif
    aria_receiving = receive_failed = incoming_listening = listening = 0;

    memset(incoming_device_name, 0, sizeof(incoming_device_name));
    memset(device_name, 0, sizeof(device_name));
    memset(&aria_playback, 0, sizeof(aria_playback));
    memset(aria_error, 0, sizeof(aria_error));
    STROOM_LOG("Starting Ethernet audio...");

    int startup = platform_network_startup(error_text, sizeof(error_text));

    if (startup != 0)
    {
        /* Compatibility failure may leave socket-client rollback pending.
         * No network reference was acquired, but close must retry that cleanup. */
        initialized = startup == PLATFORM_NETWORK_START_RESTART_REQUIRED;

        STROOM_LOG("network startup failed: %s", error_text);
        switch (startup)
        {
        case PLATFORM_NETWORK_START_RESTART_REQUIRED:
            return NETWORK_START_RESTART_REQUIRED;
        case PLATFORM_NETWORK_ERROR_CLEANUP:
            return NETWORK_ERROR_PLATFORM_CLEANUP;
        case PLATFORM_NETWORK_ERROR_MODULE_LOAD:
            return NETWORK_ERROR_MODULE_LOAD;
        case PLATFORM_NETWORK_ERROR_RPC_UNAVAILABLE:
            return NETWORK_ERROR_RPC_UNAVAILABLE;
        case PLATFORM_NETWORK_ERROR_INITIALIZE:
            return NETWORK_ERROR_INITIALIZE;
        case PLATFORM_NETWORK_ERROR_INTERFACE:
            return NETWORK_ERROR_INTERFACE;
        case PLATFORM_NETWORK_ERROR_CONFIGURE:
            return NETWORK_ERROR_CONFIGURE;
        case PLATFORM_NETWORK_ERROR_CONFIG_VERIFY:
            return NETWORK_ERROR_CONFIG_VERIFY;
        default:
            return NETWORK_START_FAILED;
        }
    }

    initialized = 1;

    refresh_address(platform_millis());
    memcpy(address_text, incoming_address, sizeof(address_text));

    aria_opened = aria_server_open(&aria, platform_socket_nonblocking) == 0;

    if (!aria_opened)
    {
        snprintf(error_text, sizeof(error_text), "%s", aria.error);
        network_close();
        return NETWORK_ERROR_SERVER_START;
    }

    /* Load sound before workers start: cold IOP initialization must not
     * consume a sender's timeout or race CD module loading. */
    const volatile int preparing = 1;
    OutputRuntime      output    = { .running = &preparing, .error = incoming_error, .capacity = sizeof(incoming_error) };

    (void)output_initialize(&output);

    STROOM_LOG("AriaCast ready");

    startup_succeeded = start_worker() == 0;

    return startup_succeeded ? 0 : NETWORK_ERROR_WORKER_START;
}

int network_poll(unsigned* generation)
{
    *generation = 0;

    if (!startup_succeeded || background.lock < 0)
    {
        return NETWORK_ERROR_UNAVAILABLE;
    }

    platform_worker_lock(&background);
    audio_analyze(&incoming, platform_millis());

    int active = receive_failed ? NETWORK_ERROR_RECEIVER : aria_receiving;

    *generation = incoming_generation;
    listening   = incoming_listening;

    memcpy(device_name, incoming_device_name, sizeof(device_name));
    snprintf(error_text, sizeof(error_text), "%s", incoming_error);
    memcpy(address_text, incoming_address, sizeof(address_text));

    if (background.running && (uint32_t)(platform_millis() - worker_progress) >= NETWORK_STALL_MS)
    {
        const char* operation = worker_operation;

        if (operation && strcmp(operation, "SOCKETS") == 0)
        {
            operation = aria.operation;
        }

        snprintf(error_text, sizeof(error_text), "NETWORK STALLED: %s", operation ? operation : "STARTING");
    }

    platform_worker_unlock(&background);

    return active;
}

int network_copy_snapshot(unsigned* generation, Audio* audio, TrackMetadata* metadata)
{
    memset(audio, 0, sizeof(*audio));
    memset(metadata, 0, sizeof(*metadata));

    *generation = 0;

    if (!startup_succeeded || background.lock < 0)
    {
        return NETWORK_ERROR_UNAVAILABLE;
    }

    platform_worker_lock(&background);

    int active = receive_failed ? NETWORK_ERROR_RECEIVER : aria_receiving;

    *generation = incoming_generation;
    listening   = incoming_listening;

    memcpy(device_name, incoming_device_name, sizeof(device_name));

    if (active > 0)
    {
        audio_analyze(&incoming, platform_millis());

        *audio    = incoming.snapshot;
        *metadata = incoming_metadata;
    }

    platform_worker_unlock(&background);

    return active;
}

#if STROOM_DIAGNOSTICS
void network_artwork_observe(const TrackMetadata* metadata, ArtworkObservation observation)
{
    if (!startup_succeeded || background.lock < 0)
    {
        return;
    }

    platform_worker_lock(&background);

    if (track_metadata_equal(metadata, &incoming_metadata))
    {
        if (artwork_pending_count < ARTWORK_PENDING_OBSERVATIONS)
        {
            artwork_pending[artwork_pending_count++] = (PendingArtworkObservation){ artwork_record(observation, incoming_metadata_revision), incoming_generation };
        }
        else
        {
            ++artwork_pending_lost;
        }
    }

    platform_worker_unlock(&background);
}

void network_artwork_display(const TrackMetadata* metadata, const char* stage)
{
    if (!startup_succeeded || background.lock < 0)
    {
        return;
    }

    platform_worker_lock(&background);

    if (track_metadata_equal(metadata, &incoming_metadata))
    {
        snprintf(incoming_artwork_display, sizeof(incoming_artwork_display), "%s", stage);
    }

    platform_worker_unlock(&background);
}
#endif

int network_take_artwork(const TrackMetadata* metadata, ArtworkBlob* blob)
{
    if (!startup_succeeded || background.lock < 0)
    {
        return 1;
    }

    platform_worker_lock(&background);

    int available = !incoming_listening && incoming_artwork.data != NULL &&
                    track_metadata_equal(metadata, &incoming_metadata) &&
                    track_metadata_equal(metadata, &incoming_artwork.metadata);

    if (available)
    {
        *blob          = incoming_artwork;
        blob->metadata = *metadata;

        memset(&incoming_artwork, 0, sizeof(incoming_artwork));
    }

    platform_worker_unlock(&background);

    return available ? 0 : 1;
}

int network_close(void)
{
    startup_succeeded = 0;

    /* Join before releasing sockets, artwork or the RPC client. */

    int result = platform_worker_close(&background);

    if (result != 0)
    {
        return result < 0 ? NETWORK_ERROR_WORKER_CLOSE : result;
    }

    artwork_receiver_close(&artwork_receiver);
    free(incoming_artwork.data);
    memset(&incoming_artwork, 0, sizeof(incoming_artwork));

    if (aria_opened)
    {
        aria_server_close(&aria);

        aria_opened = 0;
    }

    if (initialized)
    {
        if (platform_network_close() != 0)
        {
            return NETWORK_ERROR_PLATFORM_CLOSE;
        }

        initialized = 0;
    }

    return 0;
}
