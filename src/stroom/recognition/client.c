#include "recognition/client.h"
#include "audio/cd/cd.h"
#include "audio/cd/lookup/toc.h"
#include "platform/network/runtime.h"
#include "platform/network/socket_mode.h"
#include "util/ipv4.h"
#include "platform/time/clock.h"
#include "platform/time/sleep.h"
#include "platform/thread/scheduler.h"
#include "audio/common/metadata_json.h"
#include "audio/artwork/receiver.h"
#include <stdlib.h>
#include <errno.h>
#include "platform/thread/worker.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define LOOKUP_STACK_BYTES   16384
#define LOOKUP_RETRY_MS      5000u
#define LOOKUP_POLL_US       100000
#define LOOKUP_ARTWORK_READS 4
#define LOOKUP_ERROR_BYTES   128
#define REQUEST_HASH_PRIME   16777619u

static char service_host[16];

static unsigned char      stack[LOOKUP_STACK_BYTES] __attribute__((aligned(16)));
static int                socket_fd  = -1, platform_open;
static Ps2Worker          background = PS2_WORKER_INITIALIZER;
static struct sockaddr_in destination;
static char               packet[CD_LOOKUP_PACKET_BYTES];
static unsigned           packet_size, revision, received_revision, published_generation;
static int                have_disc, published_track;
static TrackMetadata      incoming_metadata;
static unsigned           incoming_artwork_state;
static uint32_t           retry_received_at, retry_delay_ms;
static ArtworkReceiver    artwork_receiver;
static ArtworkBlob        incoming_artwork;

static void accept_reply(const char* data, unsigned size, const struct sockaddr_in* peer, unsigned current)
{
    if (size < CD_LOOKUP_TOKEN_OFFSET + CD_LOOKUP_TOKEN_BYTES || size > TRACK_METADATA_JSON_BYTES + CD_LOOKUP_REPLY_HEADER_BYTES ||
        peer->sin_addr.s_addr != destination.sin_addr.s_addr || peer->sin_port != destination.sin_port)
    {
        return;
    }

    uint32_t token;

    memcpy(&token, data + CD_LOOKUP_TOKEN_OFFSET, sizeof(token));

    if (ntohl(token) != current)
    {
        return;
    }

    if (memcmp(data, CD_LOOKUP_BUSY_MAGIC, CD_LOOKUP_TOKEN_OFFSET) == 0)
    {
        if (size != CD_LOOKUP_BUSY_REPLY_BYTES)
        {
            return;
        }

        uint32_t delay;

        memcpy(&delay, data + CD_LOOKUP_BUSY_RETRY_OFFSET, sizeof(delay));

        delay = ntohl(delay);

        if (delay < CD_LOOKUP_BUSY_MIN_RETRY_MS || delay > CD_LOOKUP_BUSY_MAX_RETRY_MS)
        {
            return;
        }

        platform_worker_lock(&background);

        if (packet_size && revision == current)
        {
            retry_received_at = platform_millis();
            retry_delay_ms    = delay;
        }

        platform_worker_unlock(&background);
        return;
    }

    if (size <= CD_LOOKUP_REPLY_HEADER_BYTES || memcmp(data, CD_LOOKUP_REPLY_MAGIC, CD_LOOKUP_TOKEN_OFFSET) != 0)
    {
        return;
    }

    TrackMetadata metadata = { 0 };

    if (track_metadata_apply_json(&metadata, data + CD_LOOKUP_REPLY_HEADER_BYTES, size - CD_LOOKUP_REPLY_HEADER_BYTES) != TRACK_METADATA_UPDATE)
    {
        return;
    }

    unsigned state = (unsigned char)data[CD_LOOKUP_ARTWORK_STATE_OFFSET];

    if (state != CD_LOOKUP_ARTWORK_PENDING && state != CD_LOOKUP_ARTWORK_AVAILABLE && state != CD_LOOKUP_ARTWORK_UNAVAILABLE)
    {
        return;
    }

    if ((state == CD_LOOKUP_ARTWORK_AVAILABLE) != (metadata.artwork_url[0] != 0))
    {
        return;
    }

    platform_worker_lock(&background);

    if (packet_size && revision == current)
    {
        incoming_metadata      = metadata;
        incoming_artwork_state = state;
        received_revision      = current;
        retry_delay_ms         = 0;
    }

    platform_worker_unlock(&background);
}

static void worker(void* arg)
{
    (void)arg;

    unsigned sent_revision = 0;
    uint32_t sent_at       = 0;
    int      have_result = 0, last_error = 0;
    char     local[CD_LOOKUP_PACKET_BYTES];
    char     reply[TRACK_METADATA_JSON_BYTES + CD_LOOKUP_REPLY_HEADER_BYTES + 1];

    while (background.running)
    {
        uint32_t now = platform_millis();

        platform_worker_lock(&background);

        unsigned size = packet_size, current = revision;

        memcpy(local, packet, size);

        uint32_t      delay_ms = retry_delay_ms, delay_at = retry_received_at;
        TrackMetadata metadata = incoming_metadata;
        int           resolved = size && received_revision == current &&
                                 (incoming_artwork_state == CD_LOOKUP_ARTWORK_UNAVAILABLE ||
                                  (incoming_artwork_state == CD_LOOKUP_ARTWORK_AVAILABLE && metadata.artwork_url[0] &&
                                   artwork_receiver.complete && artwork_receiver.http.done &&
                                   track_artwork_equal(&metadata, &artwork_receiver.blob.metadata, ARTWORK_PER_URL)));

        platform_worker_unlock(&background);

        if (size && !resolved && (current != sent_revision || ((uint32_t)(now - sent_at) >= LOOKUP_RETRY_MS && (!delay_ms || (uint32_t)(now - delay_at) >= delay_ms))))
        {
            /* Retrying is optional background work; playback never waits for a reply. */
            int result = sendto(socket_fd, local, size, 0, (struct sockaddr*)&destination, sizeof(destination));
            int error  = result < 0 ? errno : 0;

            if (!have_result || error != last_error)
            {
                printf("stroom: CD lookup send: result=%d errno=%d (logging changes only)\n", result, error);

                have_result = 1;
                last_error  = error;
            }

            sent_at       = now;
            sent_revision = current;
        }

        struct sockaddr_in peer;
        socklen_t          peer_size = sizeof(peer);
        int                received  = size && !resolved ? recvfrom(socket_fd, reply, sizeof(reply), 0, (struct sockaddr*)&peer, &peer_size) : -1;

        if (received > 0 && size && peer_size == sizeof(peer))
        {
            accept_reply(reply, (unsigned)received, &peer, current);
        }

        artwork_receiver_step(&artwork_receiver, size ? &metadata : NULL, ARTWORK_PER_URL, now, platform_socket_nonblocking, LOOKUP_ARTWORK_READS);

        ArtworkBlob cover = { 0 };

        if (artwork_receiver_take(&artwork_receiver, &cover) == 0)
        {
            platform_worker_lock(&background);

            if (packet_size && track_artwork_equal(&cover.metadata, &incoming_metadata, ARTWORK_PER_URL))
            {
                free(incoming_artwork.data);

                incoming_artwork = cover;
                cover.data       = NULL;
            }

            platform_worker_unlock(&background);
            free(cover.data);
        }

        platform_sleep_us(LOOKUP_POLL_US);
    }

    platform_worker_finish(&background);
}

void cd_lookup_open(const char* host)
{
    if (background.thread >= 0 || socket_fd >= 0 || platform_open)
    {
        return;
    }

    if (!host[0])
    {
        return;
    }

    memset(&destination, 0, sizeof(destination));

    destination.sin_family = AF_INET;
    destination.sin_port   = htons(CD_LOOKUP_PORT);

    const char* end = host;
    uint32_t    address;

    if (util_ipv4_parse(&end, &address) != 0 || *end)
    {
        printf("stroom: CD lookup disabled: invalid IPv4 address\n");
        return;
    }

    snprintf(service_host, sizeof(service_host), "%s", host);

    destination.sin_addr.s_addr = htonl(address);

    char error[LOOKUP_ERROR_BYTES];

    if (platform_network_startup(error, sizeof(error)) != 0)
    {
        printf("stroom: CD lookup unavailable: %s\n", error);
        return;
    }

    platform_open = 1;
    socket_fd     = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0 || platform_socket_nonblocking(socket_fd) != 0)
    {
        goto failed;
    }

    const Ps2WorkerConfig config = { .entry = worker, .stack = stack, .stack_bytes = sizeof(stack), .priority = PS2_IO_WORKER_PRIORITY };

    if (platform_worker_open(&background, &config, NULL) != 0)
    {
        goto failed;
    }

    printf("stroom: optional CD lookup to %s:%d\n", host, CD_LOOKUP_PORT);
    return;
failed:
    printf("stroom: CD lookup unavailable; playback unaffected\n");
    cd_lookup_close();
}

void cd_lookup_poll(AudioSourceStatus* source)
{
    if (!background.running)
    {
        return;
    }

    if (source->kind != AUDIO_SOURCE_CD || !source->cd.present)
    {
        platform_worker_lock(&background);

        packet_size = received_revision = 0;
        retry_delay_ms                  = 0;
        incoming_artwork_state          = CD_LOOKUP_ARTWORK_PENDING;

        memset(&incoming_metadata, 0, sizeof(incoming_metadata));
        free(incoming_artwork.data);
        memset(&incoming_artwork, 0, sizeof(incoming_artwork));
        platform_worker_unlock(&background);

        have_disc = 0;

        return;
    }

    if (have_disc && published_generation == source->cd.generation && published_track == source->cd.track)
    {
        platform_worker_lock(&background);

        source->metadata = incoming_metadata;

        platform_worker_unlock(&background);
        return;
    }

    CdToc toc;

    if (cd_copy_toc(source->cd.generation, &toc) != 0)
    {
        return;
    }

    char     data[CD_LOOKUP_PACKET_BYTES];
    unsigned size = cd_lookup_encode(&toc, source->cd.generation, data, sizeof(data));

    if (!size)
    {
        return;
    }

    /* Mix the TOC into the token so an old service job cannot match a different
     * disc after an app restart reuses the same UDP port and generation. */
    unsigned next = revision + 1;

    for (unsigned i = 0; i < size; ++i)
    {
        next = (next ^ (unsigned char)data[i]) * REQUEST_HASH_PRIME;
    }

    if (!next)
    {
        next = 1;
    }

    int extra = snprintf(data + size - 1, sizeof(data) - size + 1, ",\"request\":%u,\"track\":%d,\"service\":\"%s\"}", next, source->cd.track, service_host);

    if (extra < 0 || (unsigned)extra >= sizeof(data) - size + 1)
    {
        return;
    }

    size = size - 1 + (unsigned)extra;

    platform_worker_lock(&background);

    if (!have_disc || published_generation != source->cd.generation)
    {
        memset(&incoming_metadata, 0, sizeof(incoming_metadata));
        free(incoming_artwork.data);
        memset(&incoming_artwork, 0, sizeof(incoming_artwork));
    }
    else
    {
        incoming_metadata.title[0] = incoming_metadata.artist[0] = 0;
    }

    source->metadata = incoming_metadata;

    memcpy(packet, data, size);

    packet_size            = size;
    revision               = next;
    received_revision      = 0;
    retry_delay_ms         = 0;
    incoming_artwork_state = CD_LOOKUP_ARTWORK_PENDING;

    platform_worker_unlock(&background);

    published_generation = source->cd.generation;
    have_disc            = 1;
    published_track      = source->cd.track;
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

    if (result != 0)
    {
        return result < 0 ? CD_LOOKUP_ERROR_WORKER_CLOSE : result;
    }

    artwork_receiver_close(&artwork_receiver);
    free(incoming_artwork.data);
    memset(&incoming_artwork, 0, sizeof(incoming_artwork));
    memset(&incoming_metadata, 0, sizeof(incoming_metadata));

    if (socket_fd >= 0)
    {
        close(socket_fd);

        socket_fd = -1;
    }

    if (platform_open)
    {
        if (platform_network_close() != 0)
        {
            return CD_LOOKUP_ERROR_NETWORK_CLOSE;
        }

        platform_open = 0;
    }

    packet_size = revision = received_revision = published_generation = 0;
    retry_delay_ms = retry_received_at = 0;
    have_disc                          = 0;
    incoming_artwork_state             = CD_LOOKUP_ARTWORK_PENDING;

    return 0;
}
