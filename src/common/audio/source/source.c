#include "audio/source/source.h"
#include "audio/network/network.h"
#include "audio/cd/cd.h"
#include "audio/cd/cd_transport.h"
#include "audio/output/device.h"
#include "platform/time/clock.h"
#include "util/diagnostics.h"
#include <stdio.h>
#include <string.h>

#define SOURCE_RETRY_MS   1000
#define CLEANUP_REPORT_MS 5000

static int      network_ready, network_started;
static int      cd_enabled;
static int      output_opened, output_restart_required;
static int      cd_autoplay;
static int      cd_closing, network_closing, output_closing;
static int      cd_start_failed, network_retry_pending;
static int      cd_restart_required, network_restart_required;
static uint32_t cd_retry_at, network_retry_at;
static int      cleanup_pending, cleanup_reported;
static uint32_t cleanup_started;
/* Backend cleanup may erase its error; retain the cause until the next start. */
static char cd_failure[AUDIO_STATUS_TEXT_BYTES];
static char network_failure[AUDIO_STATUS_TEXT_BYTES];

static const char* cd_close_message(int result)
{
    if (cd_start_failed && cd_failure[0])
    {
        return cd_failure;
    }

    return result < 0 ? "CD CLEANUP FAILED" : "STOPPING CD WORKER";
}

static const char* network_close_message(int result)
{
    if ((network_retry_pending || network_restart_required) && network_failure[0])
    {
        return network_failure;
    }

    return result < 0 ? "NETWORK CLEANUP FAILED" : "STOPPING NETWORK RECEIVER";
}

/** @brief Measure unfinished cleanup without changing resource ownership or retry policy. */
static void cleanup_result(int result)
{
    if (result != 0 && !cleanup_pending)
    {
        cleanup_pending = 1;
        cleanup_started = platform_millis();
    }
    else if (result == 0 && !cd_closing && !network_closing && !output_closing)
    {
        cleanup_pending = cleanup_reported = 0;
    }
}

/** @brief Retain CD ownership until the previous worker has been joined. */
static int close_cd(void)
{
    if (cd_enabled)
    {
        cd_closing = 1;

        int result = cd_close();

        if (result != 0)
        {
            cleanup_result(result);
            return result;
        }

        cd_enabled = cd_closing = 0;

        cleanup_result(0);
    }

    return 0;
}

/** @brief Retain the receiver and its network reference until cleanup succeeds. */
static int close_network(void)
{
    if (network_started)
    {
        network_closing = 1;

        int result = network_close();

        if (result != 0)
        {
            cleanup_result(result);
            return result;
        }

        network_started = network_ready = network_closing = 0;

        cleanup_result(0);
    }

    return 0;
}

/** @brief Withhold snapshots and sound ownership while cleanup or a source retry is pending. */
static void awaiting_close(Audio* audio, AudioSourceStatus* status, int failed, const char* message)
{
    memset(audio, 0, sizeof(*audio));
    memset(status, 0, sizeof(*status));

    if (cleanup_pending && (uint32_t)(platform_millis() - cleanup_started) >= CLEANUP_REPORT_MS)
    {
        failed  = 1;
        message = "CANNOT STOP AUDIO - RESTART CONSOLE";

        if (!cleanup_reported)
        {
            STROOM_LOG("audio cleanup taking too long; retaining resources and continuing cleanup");

            cleanup_reported = 1;
        }
    }

    status->kind = failed ? AUDIO_SOURCE_ERROR : AUDIO_SOURCE_WAITING;

    snprintf(status->error, sizeof(status->error), "%s", message);
    output_select(OUTPUT_NONE);
}

/** @brief Retain failed-start ownership until cleanup succeeds, then retry at a bounded rate. */
static void start_cd(void)
{
    cd_enabled      = 1;
    cd_start_failed = cd_open(cd_autoplay) != 0;

    snprintf(cd_failure, sizeof(cd_failure), "%s", cd_start_failed ? "CD WORKER START FAILED" : "");

    if (cd_start_failed)
    {
        cd_closing  = 1;
        cd_retry_at = platform_millis();
    }
}

int audio_source_open(int autoplay, int muted)
{
    if (output_opened || cd_enabled || network_started || output_closing || cd_start_failed)
    {
        int result = audio_source_close();

        if (result != 0)
        {
            return result;
        }
    }

    if (output_restart_required)
    {
        return AUDIO_SOURCE_ERROR_OUTPUT_RESTART_REQUIRED;
    }

    int result    = output_open();
    output_opened = result == 0;

    if (!output_opened)
    {
        output_restart_required = result == OUTPUT_ERROR_LOCK_CREATE;

        STROOM_LOG("shared output startup failed: %d", result);
        return output_restart_required ? AUDIO_SOURCE_ERROR_OUTPUT_RESTART_REQUIRED : AUDIO_SOURCE_ERROR_OUTPUT_OPEN;
    }

    audio_source_set_muted(muted);

    cd_autoplay   = autoplay;
    network_ready = network_started = 0;

    if (!cd_restart_required)
    {
        start_cd();
    }

    return 0;
}

/** @brief Consume the frame when network audio is active or receiver recovery is pending. */
static int poll_network(Audio* audio, AudioSourceStatus* status)
{
    int active = network_poll(&status->network_generation);

    if (active > 0)
    {
        active = network_copy_snapshot(&status->network_generation, audio, &status->metadata);
    }

    if (active < 0)
    {
        STROOM_LOG("network receiver failed: %s; scheduling restart", network_error());

        network_retry_pending = network_closing = 1;
        network_retry_at                        = platform_millis();

        snprintf(network_failure, sizeof(network_failure), "%s", network_error()[0] ? network_error() : "NETWORK RECEIVER FAILED");
        awaiting_close(audio, status, 1, network_failure);
        return 1;
    }

    status->kind      = AUDIO_SOURCE_NETWORK;
    status->listening = network_listening();

    snprintf(status->device_name, sizeof(status->device_name), "%s", network_device_name());

    status->network_ready   = network_ready && active >= 0;
    status->network_waiting = active <= 0;

    snprintf(status->network_address, sizeof(status->network_address), "%s", network_address());
    snprintf(status->error, sizeof(status->error), "%s", network_error());

    if (active > 0)
    {
        int result = close_cd();

        if (result != 0)
        {
            awaiting_close(audio, status, result < 0, cd_close_message(result));
            return 1;
        }
    }

    output_select(active > 0 && !status->listening ? OUTPUT_NETWORK : OUTPUT_NONE);

    return active > 0;
}

void audio_source_poll(Audio* audio, AudioSourceStatus* status)
{
    memset(status, 0, sizeof(*status));
    memset(audio, 0, sizeof(*audio));

    if (!output_opened || output_closing)
    {
        const char* message = output_restart_required ? "SOUND OUTPUT UNAVAILABLE - RESTART REQUIRED" : "SOUND OUTPUT UNAVAILABLE";

        if (cd_closing)
        {
            message = cd_close_message(-1);
        }
        else if (network_closing)
        {
            message = network_close_message(-1);
        }
        else if (output_closing)
        {
            message = "SOUND OUTPUT CLEANUP FAILED";
        }

        awaiting_close(audio, status, 1, message);
        return;
    }

    if (cd_start_failed && close_cd() != 0)
    {
        awaiting_close(audio, status, 1, cd_failure);
        return;
    }

    if (network_retry_pending)
    {
        if (close_network() != 0 || (uint32_t)(platform_millis() - network_retry_at) < SOURCE_RETRY_MS)
        {
            awaiting_close(audio, status, 1, network_failure);
            return;
        }

        network_retry_pending = 0;
    }

    int cd_result      = cd_closing ? close_cd() : 0;
    int network_result = network_closing ? close_network() : 0;

    if (cd_result != 0 || network_result != 0)
    {
        const char* message = cd_close_message(cd_result);

        if (cd_result >= 0 && network_result != 0)
        {
            message = network_close_message(network_result);
        }

        awaiting_close(audio, status, cd_result < 0 || network_result < 0, message);
        return;
    }

    /* An established stream owns the source until it stops. Resume disc
     * detection on the waiting screen, including after a listening session. */

    if (network_started && poll_network(audio, status))
    {
        return;
    }

    /* Retry CD only while network audio is idle. A cleaned-up failure leaves
     * the receiver available throughout the retry interval. */

    if (!cd_restart_required && !cd_enabled && (!cd_start_failed || (uint32_t)(platform_millis() - cd_retry_at) >= SOURCE_RETRY_MS))
    {
        start_cd();
    }

    int cd_poll_result = cd_enabled && !cd_start_failed ? cd_poll(&status->cd) : 0;

    if (cd_enabled && (cd_start_failed || cd_poll_result != 0))
    {
        if (!cd_start_failed)
        {
            cd_restart_required = cd_poll_result == CD_ERROR_RESTART_REQUIRED;

            STROOM_LOG("CD worker unavailable: %s; %s", status->cd.error, cd_restart_required ? "console restart required" : "scheduling restart");

            cd_start_failed = cd_closing = 1;
            cd_retry_at                  = platform_millis();

            snprintf(cd_failure, sizeof(cd_failure), "%s", status->cd.error[0] ? status->cd.error : "CD WORKER UNAVAILABLE");
        }

        if (close_cd() != 0)
        {
            awaiting_close(audio, status, 1, cd_failure);
            return;
        }

        memset(&status->cd, 0, sizeof(status->cd));
    }

    if (status->cd.present)
    {
        cd_copy_snapshot(&status->cd, audio);
    }

    if (status->cd.present)
    {
        int result = close_network();

        if (result != 0)
        {
            awaiting_close(audio, status, result < 0, network_close_message(result));
            return;
        }

        status->kind            = AUDIO_SOURCE_CD;
        status->network_waiting = status->network_ready = status->listening = 0;
        status->device_name[0] = status->error[0] = status->network_address[0] = 0;

        output_select(OUTPUT_CD);
        return;
    }

    if (status->cd.checking && !network_started)
    {
        status->kind = AUDIO_SOURCE_CHECKING;

        output_select(OUTPUT_NONE);
        return;
    }

    if (network_restart_required)
    {
        awaiting_close(audio, status, 1, network_failure);
        return;
    }

    if (!network_started)
    {
        output_select(OUTPUT_NONE);

        int result = network_open();

        network_ready   = result == 0;
        network_started = 1;

        if (!network_ready)
        {
            network_restart_required = result == NETWORK_START_RESTART_REQUIRED;
            network_retry_pending    = !network_restart_required;
            network_closing          = 1;
            network_retry_at         = platform_millis();

            snprintf(network_failure, sizeof(network_failure), "%s", network_error()[0] ? network_error() : "NETWORK STARTUP FAILED");
            awaiting_close(audio, status, 1, network_failure);
            return;
        }

        poll_network(audio, status);
    }
}

void audio_source_set_muted(int muted)
{
    output_set_muted(muted);
}

void audio_source_apply(const AudioSourceStatus* status, const AudioTransportRequests* requests)
{
    if (status->kind == AUDIO_SOURCE_CD)
    {
        cd_transport_apply(status->cd.generation, requests);
    }
}

int audio_source_close(void)
{
    output_opened = 0;

    output_select(OUTPUT_NONE);

    int cd_closed      = close_cd();
    int network_closed = close_network();

    if (cd_closed < 0 || network_closed < 0)
    {
        return cd_closed < 0 ? AUDIO_SOURCE_ERROR_CD_CLOSE : AUDIO_SOURCE_ERROR_NETWORK_CLOSE;
    }

    if (cd_closed > 0 || network_closed > 0)
    {
        return 1;
    }

    int result     = output_close();
    output_closing = result != 0;

    cleanup_result(result);

    if (!output_closing)
    {
        cd_start_failed = network_retry_pending = 0;

        /* Quarantined DMA buffers remain unavailable across close/open cycles. */

        if (!cd_restart_required)
        {
            cd_failure[0] = 0;
        }

        /* A close/open cycle cannot replace incompatible resident IOP modules. */

        if (!network_restart_required)
        {
            network_failure[0] = 0;
        }
    }

    return result < 0 ? AUDIO_SOURCE_ERROR_OUTPUT_CLOSE : result;
}
