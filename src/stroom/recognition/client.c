#include "recognition/client.h"
#include "recognition/toc.h"
#include "recognition/protocol.h"
#include "recognition/http.h"
#include "audio/cd/cd.h"
#include "platform/network/runtime.h"
#include "platform/time/clock.h"
#include "platform/time/sleep.h"
#include "platform/thread/scheduler.h"
#include "platform/thread/worker.h"
#include "util/ipv4.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MILLISECONDS_PER_SECOND 1000u
#define LOOKUP_EXCHANGE_MS      5000u
#define LOOKUP_PROBE_MS         3000u
#define LOOKUP_IMAGE_MS         30000u
#define JPEG_MARKER             0xff
#define JPEG_START              0xd8
#define JPEG_END                0xd9
#define JPEG_SCAN               0xda
#define JPEG_BASELINE           0xc0
#define JPEG_FRAME_LAST         0xcf
#define JPEG_HUFFMAN_TABLE      0xc4
#define JPEG_EXTENSION          0xc8
#define JPEG_ARITHMETIC_TABLE   0xcc
#define JPEG_WIDTH_OFFSET       5
#define JPEG_WIDTH_LOW_OFFSET   6

#define LOOKUP_STACK_BYTES      16384
#define LOOKUP_POLL_US          100000
#define LOOKUP_WAIT_MS          300000u
#define LOOKUP_ARTWORK_FAILURES 3
#define LOOKUP_REJECTED_PATHS   8

enum
{
    OP_NONE,
    OP_PROBE,
    OP_POST,
    OP_RESULT,
    OP_ARTWORK,
    OP_IMAGE
};

enum
{
    ATTEMPT_SUSPENDED,
    ATTEMPT_START,
    ATTEMPT_PENDING,
    ATTEMPT_COMPLETE
};

enum
{
    ART_STOP,
    ART_STATUS,
    ART_IMAGE,
    ART_COMPLETE
};

static unsigned char stack[LOOKUP_STACK_BYTES] __attribute__((aligned(16)));
static Ps2Worker     background = PS2_WORKER_INITIALIZER;
static int           platform_open;
static char          service_host[16];
static uint32_t      service_address;
/* Main-thread input and atomically published output. Socket state belongs to the worker. */
static char             packet[CD_LOOKUP_PACKET_BYTES];
static unsigned         packet_size, revision, event, expected_tracks;
static unsigned         published_generation;
static int              have_disc, published_track;
static CdLookupDocument incoming;
static ArtworkBlob      incoming_artwork;

typedef struct
{
    CdLookupHttp     http;
    CdLookupDocument reply;
    unsigned         revision, event, operation, attempt, art, failures, count;
    int              verified, network_ready, initial_probe, recovering;
    uint64_t         now, origin_at, disc_at, waiting_at;
    uint32_t         last_clock, address_at;
    char             id[CD_LOOKUP_ID_BYTES];
    char             result_path[64];
    char             image_path[CD_LOOKUP_PATH_BYTES];
    char             rejected[LOOKUP_REJECTED_PATHS][CD_LOOKUP_PATH_BYTES];
    unsigned         rejected_count;
} LookupWorker;

/**
 * @brief Select the later of two elapsed-clock deadlines.
 * @param a First deadline in milliseconds.
 * @param b Second deadline in milliseconds.
 * @return Larger deadline.
 */
static uint64_t later(uint64_t a, uint64_t b)
{
    return a > b ? a : b;
}

/**
 * @brief Convert a server delay into the worker elapsed-clock deadline.
 * @param w Worker with the current 64-bit elapsed clock.
 * @param seconds Delay in seconds.
 * @return Deadline in elapsed milliseconds.
 */
static uint64_t deadline(LookupWorker* w, uint32_t seconds)
{
    return w->now + (uint64_t)seconds * MILLISECONDS_PER_SECOND;
}

/**
 * @brief Copy one recognized track into display and artwork metadata.
 * Caller holds the worker mailbox lock while accessing the published document.
 * @param out Destination cleared when no matching track exists.
 * @param track One-based track number.
 */
static void metadata_for_track(TrackMetadata* out, unsigned track)
{
    memset(out, 0, sizeof(*out));

    if (incoming.kind != CD_DOCUMENT_MATCHED || !track || track > incoming.count)
    {
        return;
    }

    snprintf(out->title, sizeof(out->title), "%s", incoming.tracks[track - 1].title);
    snprintf(out->artist, sizeof(out->artist), "%s", incoming.tracks[track - 1].artist[0] ? incoming.tracks[track - 1].artist : incoming.artist);
    snprintf(out->album, sizeof(out->album), "%s", incoming.album);

    if (incoming.artwork.state == CD_LOOKUP_ARTWORK_AVAILABLE)
    {
        int n = snprintf(out->artwork_url, sizeof(out->artwork_url), "http://%s:%u%s", service_host, CD_LOOKUP_PORT, incoming.artwork.path);

        if (n < 0 || (unsigned)n >= sizeof(out->artwork_url))
        {
            out->artwork_url[0] = 0;
        }
    }
}

/**
 * @brief Check whether an exchange still belongs to the inserted disc.
 * @param w Worker insertion revision to compare.
 * @return Nonzero while the shared TOC revision is current and nonempty.
 */
static int current(LookupWorker* w)
{
    platform_worker_lock(&background);

    int valid = revision == w->revision && packet_size;

    platform_worker_unlock(&background);

    return valid;
}

/**
 * @brief Suspend metadata or count an artwork failure without resubmitting the TOC.
 * @param w Worker attempt and cooldown state.
 * @param invalidate Nonzero to invalidate service verification.
 */
static void suspend(LookupWorker* w, int invalidate)
{
    if (invalidate)
    {
        w->verified = 0;
    }

    if (w->operation == OP_ARTWORK || w->operation == OP_IMAGE)
    {
        ++w->failures;

        w->disc_at = later(w->disc_at, w->now + LOOKUP_EXCHANGE_MS);
    }
    else
    {
        w->attempt = ATTEMPT_SUSPENDED;
    }
}

/**
 * @brief Check a cover path against insertion-local image rejections.
 * @param w Worker rejection history.
 * @param path Origin-relative cover path.
 * @return Nonzero for a rejected path or a full rejection history.
 */
static int rejected(LookupWorker* w, const char* path)
{
    for (unsigned i = 0; i < w->rejected_count; ++i)
    {
        if (!strcmp(w->rejected[i], path))
        {
            return 1;
        }
    }

    return w->rejected_count == LOOKUP_REJECTED_PATHS;
}

/**
 * @brief Publish artwork status only if the disc revision remains current.
 * @param w Worker insertion revision.
 * @param artwork Validated artwork status to copy.
 */
static void publish_artwork(LookupWorker* w, const CdLookupArtwork* artwork)
{
    platform_worker_lock(&background);

    if (revision == w->revision && packet_size)
    {
        incoming.artwork = *artwork;
    }

    platform_worker_unlock(&background);
}

/**
 * @brief Select artwork polling or downloading and extend the disc cooldown.
 * @param w Worker recovery state.
 * @param artwork Validated artwork status.
 */
static void set_artwork(LookupWorker* w, const CdLookupArtwork* artwork)
{
    if (artwork->state == CD_LOOKUP_ARTWORK_PENDING)
    {
        w->art     = ART_STATUS;
        w->disc_at = later(w->disc_at, deadline(w, artwork->retry_seconds));
    }
    else if (artwork->state == CD_LOOKUP_ARTWORK_AVAILABLE)
    {
        if ((strcmp(w->image_path, artwork->path) != 0))
        {
            snprintf(w->image_path, sizeof(w->image_path), "%s", artwork->path);
        }

        w->art = ART_IMAGE;

        if (rejected(w, artwork->path))
        {
            w->failures = LOOKUP_ARTWORK_FAILURES;
        }
    }
    else
    {
        w->art = ART_STOP;
    }

    publish_artwork(w, artwork);
}

/**
 * @brief Remember an unusable cover path and suspend its download retries.
 * @param w Worker with the current image path.
 */
static void reject_image(LookupWorker* w)
{
    if (w->rejected_count < LOOKUP_REJECTED_PATHS)
    {
        snprintf(w->rejected[w->rejected_count++], CD_LOOKUP_PATH_BYTES, "%s", w->image_path);
    }

    w->art      = ART_IMAGE;
    w->failures = LOOKUP_ARTWORK_FAILURES;
    w->disc_at  = later(w->disc_at, w->now + LOOKUP_EXCHANGE_MS);
}

/** Reject unsupported JPEGs before passing compressed bytes to the UI decoder. */
static int valid_image(const unsigned char* data, unsigned size)
{
    if (size < 4 || data[0] != JPEG_MARKER || data[1] != JPEG_START || data[size - 2] != JPEG_MARKER || data[size - 1] != JPEG_END)
    {
        return 0;
    }

    unsigned at    = 2;
    int      frame = 0;

    while (at + 1 < size)
    {
        if (data[at++] != JPEG_MARKER)
        {
            return 0;
        }

        while (at < size && data[at] == JPEG_MARKER)
        {
            ++at;
        }

        if (at == size)
        {
            return 0;
        }

        unsigned marker = data[at++];

        if (marker == JPEG_SCAN)
        {
            return frame;
        }

        if (marker == JPEG_END || marker == 0 || marker == JPEG_START)
        {
            return 0;
        }

        if (at + 2 > size)
        {
            return 0;
        }

        unsigned length = ((unsigned)data[at] << 8) | data[at + 1];

        if (length < 2 || length > size - at)
        {
            return 0;
        }

        if (marker >= JPEG_BASELINE && marker <= JPEG_FRAME_LAST && marker != JPEG_HUFFMAN_TABLE && marker != JPEG_EXTENSION && marker != JPEG_ARITHMETIC_TABLE)
        {
            if (marker != JPEG_BASELINE || frame || length < 8 || data[at + 2] != 8)
            {
                return 0;
            }

            unsigned height = ((unsigned)data[at + 3] << 8) | data[at + 4];
            unsigned width  = ((unsigned)data[at + JPEG_WIDTH_OFFSET] << 8) | data[at + JPEG_WIDTH_LOW_OFFSET];

            if (!height || !width || height > CD_LOOKUP_COVER_SIDE || width > CD_LOOKUP_COVER_SIDE)
            {
                return 0;
            }

            frame = 1;
        }

        at += length;
    }

    return 0;
}

/**
 * @brief Validate a downloaded JPEG and transfer current-disc bytes to publication.
 * Successful publication transfers the HTTP allocation; rejected or stale bytes remain with the exchange.
 * @param w Worker owning the completed HTTP response.
 */
static void completed_image(LookupWorker* w)
{
    if (!valid_image(w->http.data, w->http.used))
    {
        reject_image(w);
        return;
    }

    platform_worker_lock(&background);

    if (revision == w->revision && packet_size)
    {
        free(incoming_artwork.data);

        incoming_artwork = (ArtworkBlob){ .data = w->http.data, .size = w->http.used };

        metadata_for_track(&incoming_artwork.metadata, 1);

        w->http.data = NULL;
    }

    platform_worker_unlock(&background);

    w->art      = ART_COMPLETE;
    w->failures = 0;
}

/**
 * @brief Apply a completed HTTP response to recognition, artwork and cooldown state.
 * @param w Worker owning the response and current operation.
 */
static void response(LookupWorker* w)
{
    CdLookupHttp*     http = &w->http;
    CdLookupDocument* doc  = &w->reply;

    if (w->operation == OP_IMAGE && http->status == LOOKUP_HTTP_OK)
    {
        completed_image(w);
        return;
    }

    int parsed = cd_lookup_document(doc, http->data, http->used);

    /* JSON is authoritative; headers only supply a fallback for unusable JSON. */
    uint32_t delay = parsed ? doc->retry_seconds : http->retry_seconds;

    if (http->status == LOOKUP_HTTP_RATE_LIMITED || http->status == LOOKUP_HTTP_UNAVAILABLE)
    {
        w->origin_at = later(w->origin_at, delay ? deadline(w, delay) : 0);
    }
    else if (http->status == LOOKUP_HTTP_PROVIDER_FAILURE || http->status == LOOKUP_HTTP_ACCEPTED || w->operation == OP_ARTWORK || w->operation == OP_IMAGE)
    {
        w->disc_at = later(w->disc_at, delay ? deadline(w, delay) : 0);
    }

    if (parsed && doc->artwork.retry_seconds)
    {
        w->disc_at = later(w->disc_at, deadline(w, doc->artwork.retry_seconds));
    }

    int throttled = http->status == LOOKUP_HTTP_RATE_LIMITED || http->status == LOOKUP_HTTP_UNAVAILABLE;
    int provider  = http->status == LOOKUP_HTTP_PROVIDER_FAILURE;
    int error     = http->status >= LOOKUP_HTTP_ERROR;

    if (!parsed || (error && doc->kind != CD_DOCUMENT_ERROR) ||
        ((throttled || provider) && (!doc->retry_seconds || !doc->retryable)) ||
        (error && !throttled && !provider && (doc->retryable || doc->retry_seconds)) ||
        (doc->id[0] && w->id[0] && (strcmp(doc->id, w->id) != 0)))
    {
        suspend(w, 1);
        return;
    }

    if (throttled)
    {
        if (!strcmp(doc->error, "service_unavailable"))
        {
            w->verified = 0;
        }

        if (w->operation == OP_PROBE || w->operation == OP_POST || (w->operation == OP_RESULT && w->recovering))
        {
            w->attempt = ATTEMPT_SUSPENDED;
        }

        return; /* Already accepted polling keeps its original waiting clock. */
    }

    if (w->operation == OP_PROBE)
    {
        if (http->status == LOOKUP_HTTP_OK && doc->kind == CD_DOCUMENT_SERVICE)
        {
            w->verified = 1;
        }
        else
        {
            suspend(w, 1);
        }

        return;
    }

    if (w->operation == OP_ARTWORK || w->operation == OP_IMAGE)
    {
        if (http->status == LOOKUP_HTTP_NOT_FOUND)
        {
            if (w->operation == OP_ARTWORK)
            {
                w->art = ART_STOP;
            }
            else
            {
                w->art = ART_STATUS;

                suspend(w, 0);
            }
        }
        else if (w->operation == OP_ARTWORK && http->status == LOOKUP_HTTP_OK && doc->kind == CD_DOCUMENT_MATCHED && !strcmp(doc->id, w->id) && doc->count == w->count)
        {
            set_artwork(w, &doc->artwork);
        }
        else
        {
            suspend(w, 1);
        }

        return;
    }

    if (http->status == LOOKUP_HTTP_ACCEPTED && doc->kind == CD_DOCUMENT_PENDING)
    {
        if (w->attempt != ATTEMPT_PENDING)
        {
            w->waiting_at = w->now;
        }

        w->attempt    = ATTEMPT_PENDING;
        w->recovering = 0;

        snprintf(w->id, sizeof(w->id), "%s", doc->id);
        snprintf(w->result_path, sizeof(w->result_path), "%s", doc->result_path);
        return;
    }

    if (http->status == LOOKUP_HTTP_OK && (doc->kind == CD_DOCUMENT_MATCHED || doc->kind == CD_DOCUMENT_NO_MATCH))
    {
        platform_worker_lock(&background);

        int valid = revision == w->revision && packet_size && (doc->kind != CD_DOCUMENT_MATCHED || doc->count == expected_tracks);

        if (valid)
        {
            incoming = *doc;
        }

        platform_worker_unlock(&background);

        if (!valid)
        {
            suspend(w, 1);
            return;
        }

        snprintf(w->id, sizeof(w->id), "%s", doc->id);
        snprintf(w->result_path, sizeof(w->result_path), "/cd/v2/lookups/%s", doc->id);

        w->attempt = ATTEMPT_COMPLETE;
        w->count   = doc->count;

        if (doc->kind == CD_DOCUMENT_MATCHED)
        {
            set_artwork(w, &doc->artwork);
        }

        return;
    }

    if (http->status == LOOKUP_HTTP_NOT_FOUND && w->operation == OP_RESULT && w->recovering)
    {
        w->id[0] = w->result_path[0] = 0;
        w->recovering                = 0;
        w->attempt                   = ATTEMPT_START; /* One fresh POST in this recovery event. */

        return;
    }

    if (http->status == LOOKUP_HTTP_NOT_FOUND || provider)
    {
        w->id[0] = w->result_path[0] = 0;
    }

    suspend(w, !error);
}

/**
 * @brief Start an HTTP operation within its per-request and remaining lookup budgets.
 * @param w Worker attempt and clock state.
 * @param operation OP_* operation kind.
 * @param path Origin-relative API or image path.
 * @param body JSON body for POST, or NULL for GET.
 */
static void begin(LookupWorker* w, unsigned operation, const char* path, const char* body)
{
    unsigned budget = operation == OP_PROBE ? LOOKUP_PROBE_MS : operation == OP_IMAGE ? LOOKUP_IMAGE_MS
                                                                                      : LOOKUP_EXCHANGE_MS;

    if (w->attempt == ATTEMPT_PENDING && (operation == OP_RESULT))
    {
        uint64_t remaining = LOOKUP_WAIT_MS - (w->now - w->waiting_at);

        if (remaining < budget)
        {
            budget = (unsigned)remaining;
        }
    }

    w->operation = operation;

    if (cd_lookup_http_open(&w->http, service_host, service_address, path, body, budget, (uint32_t)w->now) < 0)
    {
        suspend(w, 1);
        cd_lookup_http_close(&w->http);

        w->operation = OP_NONE;
    }
}

/**
 * @brief Run disc-scoped recognition and artwork exchanges on the background worker.
 * @param arg Unused worker argument.
 */
static void worker(void* arg)
{
    (void)arg;

    LookupWorker* w = calloc(1, sizeof(*w));

    if (!w)
    {
        platform_worker_finish(&background);
        return;
    }

    w->initial_probe = 1;
    w->last_clock    = platform_millis();
    w->http.fd       = -1;

    while (background.running)
    {
        uint32_t clock = platform_millis();
        w->now += (uint32_t)(clock - w->last_clock);
        w->last_clock = clock;
        char body[CD_LOOKUP_PACKET_BYTES];

        platform_worker_lock(&background);

        unsigned rev = revision, events = event, size = packet_size;

        memcpy(body, packet, size);

        body[size] = 0;

        platform_worker_unlock(&background);

        if (w->revision != rev)
        {
            cd_lookup_http_close(&w->http);

            w->revision  = rev;
            w->operation = OP_NONE;
            w->attempt   = size && w->now >= w->origin_at ? ATTEMPT_START : ATTEMPT_SUSPENDED;
            w->art       = ART_STOP;
            w->failures = w->rejected_count = 0;
            w->recovering                   = 0;
            w->id[0] = w->result_path[0] = w->image_path[0] = 0;
            w->disc_at = w->waiting_at = 0;
        }

        if (w->event != events)
        {
            w->event = events;

            if (size && !w->operation && w->now >= w->origin_at)
            {
                if (w->attempt == ATTEMPT_SUSPENDED && w->now >= w->disc_at)
                {
                    w->attempt    = ATTEMPT_START;
                    w->recovering = w->id[0] != 0;
                }

                if (w->attempt == ATTEMPT_COMPLETE && w->failures >= LOOKUP_ARTWORK_FAILURES && w->rejected_count < LOOKUP_REJECTED_PATHS && w->now >= w->disc_at)
                {
                    w->failures = 0;
                    w->art      = ART_STATUS;
                }
            }
        }

        if (!w->network_ready && (!w->address_at || (uint32_t)(clock - w->address_at) >= MILLISECONDS_PER_SECOND))
        {
            char address[16];

            platform_network_ready_address(address, sizeof(address));

            /* The readiness RPC may block; HTTP budgets start after it returns. */
            uint32_t after = platform_millis();
            w->now += (uint32_t)(after - w->last_clock);
            w->last_clock = after;

            w->network_ready = address[0] != 0;
            w->address_at    = after ? after : 1;
        }

        if (w->attempt == ATTEMPT_PENDING && w->now - w->waiting_at >= LOOKUP_WAIT_MS)
        {
            cd_lookup_http_close(&w->http);

            w->operation = OP_NONE;
            w->attempt   = ATTEMPT_SUSPENDED;
        }

        if (w->operation)
        {
            int      result = cd_lookup_http_step(&w->http, (uint32_t)w->now);
            uint32_t after  = platform_millis();
            w->now += (uint32_t)(after - w->last_clock);
            w->last_clock = after;

            if ((uint32_t)((uint32_t)w->now - w->http.started) >= w->http.budget || (w->attempt == ATTEMPT_PENDING && w->now - w->waiting_at >= LOOKUP_WAIT_MS))
            {
                result = -1;
            }

            if (result <= 0)
            {
                if (w->operation == OP_PROBE)
                {
                    w->initial_probe = 0;
                }

                if (result < 0)
                {
                    uint64_t at = w->http.retry_seconds ? deadline(w, w->http.retry_seconds) : 0;

                    if (w->http.status == LOOKUP_HTTP_RATE_LIMITED || w->http.status == LOOKUP_HTTP_UNAVAILABLE)
                    {
                        w->origin_at = later(w->origin_at, at);
                    }
                    else
                    {
                        w->disc_at = later(w->disc_at, at);
                    }

                    if (w->operation == OP_IMAGE && w->http.failed == 2)
                    {
                        reject_image(w);
                    }
                    else
                    {
                        suspend(w, 1);
                    }
                }
                else if (w->operation == OP_PROBE || current(w))
                {
                    response(w);
                }

                cd_lookup_http_close(&w->http);

                w->operation = OP_NONE;
            }
        }
        else if (w->network_ready && w->now >= w->origin_at && (!size || w->now >= w->disc_at))
        {
            if (!w->verified && (w->initial_probe || (size && w->attempt == ATTEMPT_START)))
            {
                begin(w, OP_PROBE, "/cd/v2/service", NULL);
            }
            else if (size && w->verified && w->attempt == ATTEMPT_START)
            {
                begin(w, w->id[0] ? OP_RESULT : OP_POST, w->id[0] ? w->result_path : "/cd/v2/lookups", w->id[0] ? NULL : body);
            }
            else if (size && w->attempt == ATTEMPT_PENDING)
            {
                begin(w, OP_RESULT, w->result_path, NULL);
            }
            else if (size && w->attempt == ATTEMPT_COMPLETE && w->failures < LOOKUP_ARTWORK_FAILURES)
            {
                if (w->art == ART_STATUS)
                {
                    begin(w, OP_ARTWORK, w->result_path, NULL);
                }
                else if (w->art == ART_IMAGE && !rejected(w, w->image_path))
                {
                    char url[METADATA_ARTWORK_URL_BYTES];
                    int  n = snprintf(url, sizeof(url), "http://%s:%u%s", service_host, CD_LOOKUP_PORT, w->image_path);

                    if (n < 0 || (unsigned)n >= sizeof(url))
                    {
                        reject_image(w);
                    }
                    else
                    {
                        begin(w, OP_IMAGE, w->image_path, NULL);
                    }
                }
            }
        }

        platform_sleep_us(LOOKUP_POLL_US);
    }

    cd_lookup_http_close(&w->http);
    free(w);
    platform_worker_finish(&background);
}

void cd_lookup_open(const char* host)
{
    if (background.thread >= 0 || platform_open || !host[0])
    {
        return;
    }

    const char* end = host;

    if (util_ipv4_parse(&end, &service_address) || *end)
    {
        printf("stroom: CD lookup disabled: invalid IPv4 address\n");
        return;
    }

    snprintf(service_host, sizeof(service_host), "%s", host);

    char error[128];

    if (platform_network_startup(error, sizeof(error)))
    {
        printf("stroom: CD lookup unavailable: %s\n", error);
        return;
    }

    platform_open                = 1;
    const Ps2WorkerConfig config = { .entry = worker, .stack = stack, .stack_bytes = sizeof(stack), .priority = PS2_IO_WORKER_PRIORITY };

    if (platform_worker_open(&background, &config, NULL))
    {
        cd_lookup_close();
        return;
    }

    printf("stroom: optional CD HTTP lookup to %s:%u\n", host, CD_LOOKUP_PORT);
}

void cd_lookup_poll(AudioSourceStatus* source)
{
    if (!background.running)
    {
        return;
    }

    if (source->kind != AUDIO_SOURCE_CD || !source->cd.present)
    {
        /* UI/source transitions can hide a valid disc temporarily. Confirm its
         * TOC is gone before ending the insertion's recognition lifecycle. */
        CdToc existing;

        if (have_disc && cd_copy_toc(published_generation, &existing) == 0)
        {
            return;
        }

        if (have_disc)
        {
            platform_worker_lock(&background);

            packet_size = 0;

            ++revision;
            memset(&incoming, 0, sizeof(incoming));
            free(incoming_artwork.data);
            memset(&incoming_artwork, 0, sizeof(incoming_artwork));
            platform_worker_unlock(&background);

            have_disc = 0;
        }

        return;
    }

    if (!have_disc || published_generation != source->cd.generation)
    {
        CdToc toc;
        char  data[CD_LOOKUP_PACKET_BYTES];

        if (cd_copy_toc(source->cd.generation, &toc))
        {
            return;
        }

        unsigned size = cd_lookup_encode(&toc, data, sizeof(data));

        if (!size)
        {
            return;
        }

        platform_worker_lock(&background);
        memcpy(packet, data, size);

        packet_size     = size;
        expected_tracks = (unsigned)toc.count;

        ++revision;
        ++event;
        memset(&incoming, 0, sizeof(incoming));
        free(incoming_artwork.data);
        memset(&incoming_artwork, 0, sizeof(incoming_artwork));
        platform_worker_unlock(&background);

        published_generation = source->cd.generation;
        published_track      = source->cd.track;
        have_disc            = 1;
    }
    else if (published_track != source->cd.track)
    {
        platform_worker_lock(&background);
        ++event;
        platform_worker_unlock(&background);

        published_track = source->cd.track;
    }

    platform_worker_lock(&background);
    metadata_for_track(&source->metadata, (unsigned)source->cd.track);
    platform_worker_unlock(&background);
}

int cd_lookup_take_artwork(const TrackMetadata* metadata, ArtworkBlob* blob)
{
    if (background.lock < 0)
    {
        return 1;
    }

    platform_worker_lock(&background);

    int available = incoming_artwork.data && track_artwork_equal(metadata, &incoming_artwork.metadata, ARTWORK_PER_URL);

    if (available)
    {
        *blob          = incoming_artwork;
        blob->metadata = *metadata;

        memset(&incoming_artwork, 0, sizeof(incoming_artwork));
    }

    platform_worker_unlock(&background);

    return available ? 0 : 1;
}

int cd_lookup_close(void)
{
    int result = platform_worker_close(&background);

    if (result)
    {
        return result < 0 ? CD_LOOKUP_ERROR_WORKER_CLOSE : result;
    }

    free(incoming_artwork.data);
    memset(&incoming_artwork, 0, sizeof(incoming_artwork));
    memset(&incoming, 0, sizeof(incoming));

    if (platform_open)
    {
        if (platform_network_close())
        {
            return CD_LOOKUP_ERROR_NETWORK_CLOSE;
        }

        platform_open = 0;
    }

    packet_size = revision = event = expected_tracks = published_generation = 0;
    published_track = have_disc = 0;

    return 0;
}
