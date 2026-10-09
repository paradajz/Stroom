#include "util/diagnostics.h"
#include "platform/audio/output.h"
#include <kernel.h>
#include "platform/iop/modules.h"
#include "platform/iop/services.h"
#include "contracts/audio.h"
#include "platform/time/clock.h"
#include <audsrv.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* audsrv ring size and bounded queue depth, in bytes. */
#define OUTPUT_RING_BYTES         20480
#define OUTPUT_PREPARE_TIMEOUT_MS 1000
#define OUTPUT_POLL_DELAY_US      1000

extern unsigned char             audsrv_irx[], freesd_irx[];
extern unsigned int              size_audsrv_irx, size_freesd_irx;
static uint8_t                   silence[PS2_AUDIO_OUTPUT_SILENCE_BYTES];
static int                       lock = -1;
static int                       ready;
static int                       service_pending;
static int                       closing, close_stopped;
static Ps2AudioProducer          active_owner;
static volatile Ps2AudioProducer selected;
static volatile int              requested_mute;
static int                       applied_mute = -1;
static volatile int              refill_pending;
static int                       queued_bytes = -1;

/* The SDK calls this on its EE callback thread. Never take the output lock
 * or make a sound RPC: the producer may already be waiting on the IOP. */
static int refill(void* argument)
{
    (void)argument;

    refill_pending = 1;

    return 0;
}

/* Clear before querying so a notification during the RPC remains pending.
 * Successful writes add to this snapshot; until the next notification it
 * conservatively overstates queued audio. Call under the output lock. */
static int refresh_queue(void)
{
    if (queued_bytes < 0 || refill_pending)
    {
        refill_pending = 0;
        int queried    = audsrv_queued();
        queued_bytes   = queried < 0 ? PS2_AUDIO_ERROR_QUERY : queried;
    }

    return queued_bytes;
}

static int apply_volume(void)
{
    int muted = requested_mute;

    if (applied_mute == muted)
    {
        return 0;
    }

    if (audsrv_set_volume(muted ? 0 : MAX_VOLUME) != 0)
    {
        return PS2_AUDIO_ERROR_VOLUME;
    }

    applied_mute = muted;

    return 0;
}

void platform_audio_output_set_muted(int muted)
{
    requested_mute = !!muted;
}

/**
 * @brief Report initialization progress.
 * @param runtime Worker callbacks.
 * @param message Progress text.
 */
static void stage(const Ps2AudioOutputRuntime* runtime, const char* message)
{
    if (runtime->stage)
    {
        runtime->stage(runtime->context, message);
    }
}

/** @brief Release partial sound startup, retaining ownership if cleanup fails. */
static void rollback_sound(void)
{
    int result = audsrv_quit();

    if (result != 0)
    {
        closing = 1;

        STROOM_LOG("sound startup rollback audsrv_quit failed: %d", result);
    }
    else
    {
        service_pending = 0;
    }
}

/**
 * @brief Initialize audsrv for 48 kHz stereo PCM16.
 * @param runtime Worker lifetime and progress callbacks.
 * @return 0 on success; negative on failure or cancellation.
 */
static int initialize_sound(const Ps2AudioOutputRuntime* runtime)
{
    stage(runtime, "LOADING SOUND DRIVER");

    if (platform_iop_module(&(Ps2IopModule){ .name = "libsd", .data = freesd_irx, .size = size_freesd_irx }, NULL, 0) != 0)
    {
        snprintf(runtime->error, runtime->capacity, "SOUND DRIVER LOAD FAILED");
        return PS2_AUDIO_ERROR_DRIVER_LOAD;
    }

    stage(runtime, "LOADING SOUND SERVICE");

    if (platform_iop_module(&(Ps2IopModule){ .name = "audsrv", .data = audsrv_irx, .size = size_audsrv_irx }, NULL, 0) != 0)
    {
        snprintf(runtime->error, runtime->capacity, "SOUND SERVICE LOAD FAILED");
        return PS2_AUDIO_ERROR_SERVICE_LOAD;
    }

    stage(runtime, "BINDING SOUND SERVICE");

    for (int i = 0; i < PS2_RPC_PROBE_ATTEMPTS && *runtime->running; ++i)
    {
        if (platform_iop_probe(AUDSRV_IRX) > 0)
        {
            stage(runtime, "INITIALIZING SOUND SERVICE");

            service_pending = 1;
            int result      = audsrv_init();

            if (result != 0)
            {
                snprintf(runtime->error, runtime->capacity, "SOUND INIT ERROR %d", result);
                rollback_sound();
                return PS2_AUDIO_ERROR_INITIALIZE;
            }

            applied_mute = -1;

            stage(runtime, "SETTING AUDIO FORMAT");

            audsrv_fmt_t format = { AUDIO_RATE, 16, 2 };

            result = audsrv_set_format(&format);

            if (result != 0)
            {
                snprintf(runtime->error, runtime->capacity, "SOUND FORMAT ERROR %d", result);
                rollback_sound();
                return PS2_AUDIO_ERROR_FORMAT;
            }

            stage(runtime, "SETTING AUDIO VOLUME");

            result = apply_volume();

            if (result != 0)
            {
                snprintf(runtime->error, runtime->capacity, "SOUND VOLUME ERROR %d", result);
                rollback_sound();
                return PS2_AUDIO_ERROR_VOLUME;
            }

            return 0;
        }

        usleep(PS2_RPC_PROBE_DELAY_US);
    }

    snprintf(runtime->error, runtime->capacity, "SOUND RPC UNAVAILABLE");

    return *runtime->running ? PS2_AUDIO_ERROR_RPC_UNAVAILABLE : PS2_AUDIO_ERROR_CANCELLED;
}

/**
 * @brief Overwrite the 20 KiB audsrv ring while muted, leaving a short silent lead-in.
 * Stopping audsrv does not flush its ring.
 * @param runtime Worker lifetime callbacks.
 * @return 0 after flushing; negative on failure, timeout, or cancellation.
 */
static int prepare_output(const Ps2AudioOutputRuntime* runtime)
{
    if (audsrv_stop_audio() != 0 || audsrv_set_volume(0) != 0)
    {
        return PS2_AUDIO_ERROR_PREPARE;
    }

    applied_mute = 1;

    audsrv_fmt_t format = { AUDIO_RATE, 16, 2 };

    if (audsrv_set_format(&format) != 0)
    {
        return PS2_AUDIO_ERROR_PREPARE;
    }

    unsigned written = 0;
    uint32_t started = platform_millis();

    while (*runtime->running && platform_millis() - started < OUTPUT_PREPARE_TIMEOUT_MS)
    {
        if (written < OUTPUT_RING_BYTES)
        {
            int available = audsrv_available();

            if (available < 0)
            {
                break;
            }

            if (available > (int)sizeof(silence))
            {
                if (audsrv_play_audio((const char*)silence, sizeof(silence)) != sizeof(silence))
                {
                    break;
                }

                written += sizeof(silence);
            }
        }

        if (written == OUTPUT_RING_BYTES)
        {
            int queued = audsrv_queued();

            if (queued < 0)
            {
                break;
            }

            if (queued <= PS2_AUDIO_OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES)
            {
                if (audsrv_stop_audio() != 0 || apply_volume() != 0)
                {
                    break;
                }

                /* stop_audio clears the threshold. Arm after the final stop;
                 * the first write queries directly because playback is stopped. */
                queued_bytes   = -1;
                refill_pending = 0;

                if (audsrv_on_fillbuf(OUTPUT_RING_BYTES - PS2_AUDIO_OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES, refill, NULL) != 0)
                {
                    break;
                }

                return 0;
            }
        }

        usleep(OUTPUT_POLL_DELAY_US);
    }

    /* Best-effort cleanup; preparation has already failed. */
    audsrv_stop_audio();

    return PS2_AUDIO_ERROR_PREPARE;
}

int platform_audio_output_open(void)
{
    if (closing)
    {
        return PS2_AUDIO_ERROR_CLEANUP_PENDING;
    }

    if (lock >= 0)
    {
        return 0;
    }

    ee_sema_t sema = { 0 };

    sema.init_count = sema.max_count = 1;
    lock                             = CreateSema(&sema);
    selected = active_owner = PS2_AUDIO_OUTPUT_NONE;
    requested_mute          = 0;
    applied_mute            = -1;

    if (lock < 0)
    {
        STROOM_LOG("sound startup CreateSema(lock) failed: %d", lock);
    }

    return (lock >= 0) ? 0 : PS2_AUDIO_ERROR_LOCK_CREATE;
}

void platform_audio_output_select(Ps2AudioProducer owner)
{
    selected = owner;
}

int platform_audio_output_selected(Ps2AudioProducer owner)
{
    return selected == owner;
}

int platform_audio_output_initialize(const Ps2AudioOutputRuntime* runtime)
{
    if (lock < 0)
    {
        snprintf(runtime->error, runtime->capacity, "SOUND LOCK UNAVAILABLE");
        return PS2_AUDIO_ERROR_LOCK_UNAVAILABLE;
    }

    WaitSema(lock);

    if (closing)
    {
        snprintf(runtime->error, runtime->capacity, "SOUND CLEANUP PENDING");
        SignalSema(lock);
        return PS2_AUDIO_ERROR_CLEANUP_PENDING;
    }

    int result = 0;

    if (!ready)
    {
        result = initialize_sound(runtime);
        ready  = result == 0;
    }

    SignalSema(lock);

    return result;
}

int platform_audio_output_prepare(Ps2AudioProducer owner, const Ps2AudioOutputRuntime* runtime)
{
    if (lock < 0)
    {
        return PS2_AUDIO_ERROR_LOCK_UNAVAILABLE;
    }

    WaitSema(lock);

    int result = 1;

    if (ready && selected == owner)
    {
        active_owner = PS2_AUDIO_OUTPUT_NONE;
        queued_bytes = -1;
        result       = prepare_output(runtime);

        if (selected != owner)
        {
            result = 1;

            (void)audsrv_stop_audio();
        }

        if (result == 0)
        {
            active_owner = owner;
        }
    }

    SignalSema(lock);

    return result;
}

int platform_audio_output_stop(Ps2AudioProducer owner)
{
    if (lock < 0)
    {
        return 0;
    }

    WaitSema(lock);

    int result = 0;

    if (ready && active_owner == owner)
    {
        result = audsrv_stop_audio() == 0 ? 0 : PS2_AUDIO_ERROR_STOP;

        if (result == 0)
        {
            active_owner   = PS2_AUDIO_OUTPUT_NONE;
            queued_bytes   = -1;
            refill_pending = 0;
        }
    }

    SignalSema(lock);

    return result;
}

/**
 * @brief Shared output admission with optional timing of existing operations.
 * @param owner Producer.
 * @param pcm Audio bytes.
 * @param bytes Byte count.
 * @param timing Optional timing destination.
 * @return Zero accepted, positive busy, negative failed.
 */
static int write_output(Ps2AudioProducer owner, const uint8_t* pcm, unsigned bytes, Ps2AudioOutputWriteTiming* timing)
{
    (void)timing;

    if (lock < 0)
    {
        return PS2_AUDIO_ERROR_LOCK_UNAVAILABLE;
    }

    if (!bytes || bytes > PS2_AUDIO_OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES)
    {
        return PS2_AUDIO_ERROR_INVALID_WRITE;
    }

#if STROOM_DIAGNOSTICS
    uint32_t at = timing ? platform_millis() : 0;
#endif
    WaitSema(lock);
#if STROOM_DIAGNOSTICS
    if (timing)
    {
        timing->lock_ms = platform_millis() - at;
    }
#endif

    int result = 1;

    if (ready && active_owner == owner && selected == owner)
    {
#if STROOM_DIAGNOSTICS
        at = timing ? platform_millis() : 0;
#endif
        int queried = queued_bytes < 0 || refill_pending;
        int volume  = apply_volume();
        int queued  = volume == 0 ? refresh_queue() : volume;

        (void)queried;
#if STROOM_DIAGNOSTICS
        if (timing)
        {
            timing->queued    = queried ? queued : -1;
            timing->queued_ms = platform_millis() - at;
            at                = platform_millis();
        }
#endif

        int available = queued >= 0 && queued <= PS2_AUDIO_OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES ? audsrv_available() : 0;
#if STROOM_DIAGNOSTICS
        if (timing)
        {
            timing->available_ms = platform_millis() - at;
        }
#endif

        if (queued < 0 || available < 0)
        {
            result = queued < 0 ? queued : PS2_AUDIO_ERROR_QUERY;
        }
        else if (queued <= PS2_AUDIO_OUTPUT_QUEUE_ADMISSION_THRESHOLD_BYTES && available > (int)bytes)
        {
#if STROOM_DIAGNOSTICS
            at = timing ? platform_millis() : 0;
#endif
            result       = audsrv_play_audio((const char*)pcm, bytes) == (int)bytes ? 0 : PS2_AUDIO_ERROR_WRITE;
            queued_bytes = result == 0 ? queued + (int)bytes : -1;
#if STROOM_DIAGNOSTICS
            if (timing)
            {
                timing->submit_ms = platform_millis() - at;
            }
#endif
        }
    }

    SignalSema(lock);

    return result;
}

int platform_audio_output_write(Ps2AudioProducer owner, const uint8_t* pcm, unsigned bytes)
{
    return write_output(owner, pcm, bytes, NULL);
}

#if STROOM_DIAGNOSTICS
int platform_audio_output_write_timed(Ps2AudioProducer owner, const uint8_t* pcm, unsigned bytes, Ps2AudioOutputWriteTiming* timing)
{
    memset(timing, 0, sizeof(*timing));

    timing->queued = -1;
    timing->begin  = platform_millis();

    int result = write_output(owner, pcm, bytes, timing);

    timing->end = platform_millis();

    return result;
}
#endif

int platform_audio_output_queued(Ps2AudioProducer owner)
{
    if (lock < 0)
    {
        return 0;
    }

    WaitSema(lock);

    int result = 0;

    if (ready && active_owner == owner)
    {
        result = apply_volume();

        if (result == 0)
        {
            result = refresh_queue();
        }
    }

    SignalSema(lock);

    return result;
}

int platform_audio_output_close(void)
{
    if (lock < 0)
    {
        return 0;
    }

    closing  = 1;
    selected = PS2_AUDIO_OUTPUT_NONE;

    if (service_pending)
    {
        if (ready && !close_stopped)
        {
            int result = audsrv_stop_audio();

            if (result != 0)
            {
                STROOM_LOG("sound cleanup audsrv_stop_audio failed: %d", result);
                return PS2_AUDIO_ERROR_STOP;
            }

            close_stopped = 1;
        }

        int result = audsrv_quit();

        if (result != 0)
        {
            STROOM_LOG("sound cleanup audsrv_quit failed: %d", result);
            return PS2_AUDIO_ERROR_QUIT;
        }

        ready = service_pending = 0;
    }

    int result = DeleteSema(lock);

    if (result < 0)
    {
        STROOM_LOG("sound cleanup DeleteSema id=%d failed: %d", lock, result);
        return PS2_AUDIO_ERROR_LOCK_DELETE;
    }

    lock           = -1;
    queued_bytes   = -1;
    refill_pending = 0;
    selected = active_owner = PS2_AUDIO_OUTPUT_NONE;
    closing = close_stopped = 0;

    return 0;
}
