#include "recognition/client.h"
#include "audio/cd/cd.h"
#include "audio/common/metadata_json.h"
#include "audio/artwork/receiver.h"
#include "platform/network/runtime.h"
#include "platform/thread/scheduler.h"
#include "support/worker_driver.h"
#include "contracts/cd.h"
#include "unity.h"
#include <kernel.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int                       _gp;
static int                network_close_ok;
static unsigned           clock_ms, sends, receives, sleeps, closes, thread_creates;
static int                startup_ok, socket_ok, next_sema, status_failure;
static CdToc              disc;
static AudioSourceStatus  source;
static struct sockaddr_in service, reply_peer;
static unsigned           request_token, old_token, reply_state, auto_reply, reply_queued;
static char               reply[TRACK_METADATA_JSON_BYTES + CD_LOOKUP_REPLY_HEADER_BYTES];
static unsigned           reply_size;
static int                artwork_complete;
static void (*sleep_hook)(void);
static unsigned stop_after;

static void queue_reply(unsigned token, const char* json)
{
    memcpy(reply, CD_LOOKUP_REPLY_MAGIC, CD_LOOKUP_TOKEN_OFFSET);

    uint32_t wire_token = htonl(token);

    memcpy(reply + CD_LOOKUP_TOKEN_OFFSET, &wire_token, sizeof(wire_token));

    reply[CD_LOOKUP_ARTWORK_STATE_OFFSET] = reply_state;

    memcpy(reply + CD_LOOKUP_REPLY_HEADER_BYTES, json, strlen(json));

    reply_size   = CD_LOOKUP_REPLY_HEADER_BYTES + strlen(json);
    reply_peer   = service;
    reply_queued = 1;
}

static void cover_reply(unsigned token)
{
    queue_reply(token, reply_state == CD_LOOKUP_ARTWORK_AVAILABLE ? "{\"type\":\"metadata\",\"data\":{\"title\":\"Track\",\"album\":\"Album\",\"artwork_url\":\"http://192.0.2.1/cover.jpg\"}}" : "{\"type\":\"metadata\",\"data\":{\"title\":\"Track\",\"album\":\"Album\",\"artwork_url\":\"\"}}");
}

ssize_t __wrap_recvfrom(int fd, void* data, size_t size, int flags, struct sockaddr* peer, socklen_t* length)
{
    (void)fd;
    (void)flags;
    ++receives;

    if (auto_reply && !reply_queued)
    {
        cover_reply(request_token);

        auto_reply = 0;
    }

    if (reply_queued)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT(reply_size, size);
        memcpy(data, reply, reply_size);
        memcpy(peer, &reply_peer, sizeof(reply_peer));

        *length      = sizeof(reply_peer);
        reply_queued = 0;

        return reply_size;
    }

    errno = EWOULDBLOCK;

    return -1;
}

void artwork_receiver_step(ArtworkReceiver* receiver, const TrackMetadata* metadata, ArtworkIdentity identity, uint32_t now, int (*nonblocking)(int), unsigned read_limit)
{
    TEST_ASSERT_EQUAL_INT(ARTWORK_PER_URL, identity);
    TEST_ASSERT_EQUAL_UINT(4, read_limit);
    (void)now;
    (void)nonblocking;

    if (metadata && artwork_complete)
    {
        receiver->blob.metadata = *metadata;
        receiver->complete = receiver->http.done = 1;
    }
}

int artwork_receiver_take(ArtworkReceiver* receiver, ArtworkBlob* result)
{
    (void)receiver;
    (void)result;

    return 1;
}

void artwork_receiver_close(ArtworkReceiver* receiver)
{
    memset(receiver, 0, sizeof(*receiver));
}

int platform_network_startup(char* error, size_t capacity)
{
    snprintf(error, capacity, "offline");

    return startup_ok ? 0 : -1;
}

int platform_network_close(void)
{
    ++closes;

    return network_close_ok ? 0 : -1;
}

uint32_t platform_millis(void)
{
    return clock_ms;
}

int cd_copy_toc(unsigned generation, CdToc* toc)
{
    (void)generation;

    *toc = disc;

    return (disc.count > 0) ? 0 : -1;
}

int __wrap_socket(int domain, int type, int protocol)
{
    TEST_ASSERT_EQUAL_INT(AF_INET, domain);
    TEST_ASSERT_EQUAL_INT(SOCK_DGRAM, type);
    (void)protocol;

    return socket_ok ? 10 : -1;
}

int __wrap_close(int fd)
{
    TEST_ASSERT_EQUAL_INT(10, fd);

    return 0;
}

int _ps2sdk_ioctl(int fd, int command, void* value)
{
    (void)fd;
    TEST_ASSERT_EQUAL_INT(FIONBIO, command);
    TEST_ASSERT_EQUAL_UINT(1, *(unsigned long*)value);

    return 0;
}

ssize_t __wrap_sendto(int fd, const void* data, size_t size, int flags, const struct sockaddr* address, socklen_t length)
{
    (void)fd;
    (void)flags;
    (void)length;

    service = *(const struct sockaddr_in*)address;

    TEST_ASSERT_EQUAL_UINT(CD_LOOKUP_PORT, ntohs(service.sin_port));

    char text[CD_LOOKUP_PACKET_BYTES];

    TEST_ASSERT_LESS_THAN_UINT(sizeof(text), size);
    memcpy(text, data, size);

    text[size] = 0;

    TEST_ASSERT_NOT_NULL(strstr(text, "\"offsets\":[150]"));

    const char* token = strstr(text, "\"request\":");

    TEST_ASSERT_NOT_NULL(token);

    request_token = (unsigned)strtoul(token + 10, NULL, 10);

    ++sends;

    errno = EHOSTUNREACH;

    return -1;
}

int __wrap_usleep(useconds_t duration)
{
    TEST_ASSERT_EQUAL_UINT(100000, duration);

    clock_ms += 100;

    ++sleeps;
    cd_lookup_poll(&source);

    if (sleep_hook)
    {
        sleep_hook();
    }

    if (sleeps >= stop_after)
    {
        test_worker_stop();
    }

    return 0;
}

int CreateSema(ee_sema_t* sema)
{
    (void)sema;

    return ++next_sema;
}

int WaitSema(int id)
{
    (void)id;

    return 0;
}

int SignalSema(int id)
{
    (void)id;

    return 0;
}

int DeleteSema(int id)
{
    (void)id;

    return 0;
}

int CreateThread(ee_thread_t* thread)
{
    TEST_ASSERT_NOT_NULL(thread->func);
    ++thread_creates;

    return 1;
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
    (void)__wrap_usleep(microseconds);
}

void setUp(void)
{
    test_worker_reset();

    status_failure = 0;
    startup_ok = socket_ok = network_close_ok = 1;
    reply_state                               = CD_LOOKUP_ARTWORK_AVAILABLE;
    auto_reply = reply_queued = artwork_complete = 0;
    clock_ms = sends = receives = sleeps = closes = next_sema = thread_creates = 0;
    request_token = old_token = 0;
    stop_after                = 62;
    sleep_hook                = NULL;
    disc                      = (CdToc){ .count = 1, .start = { 0, 1000 } };
    source                    = (AudioSourceStatus){ .kind = AUDIO_SOURCE_CD, .cd = { .present = 1, .generation = 1, .track = 1 } };
}

void tearDown(void)
{
    status_failure   = 0;
    network_close_ok = 1;

    TEST_ASSERT_TRUE(cd_lookup_close() == 0);
}

static void open_client(void)
{
    cd_lookup_open("192.0.2.1");
    cd_lookup_poll(&source);
    TEST_ASSERT_LESS_THAN_INT(PS2_RENDER_PRIORITY, test_worker_priority());
}

static void absent_service_is_harmless(void)
{
    open_client();
    TEST_ASSERT_EQUAL_UINT(0, sends);
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(2, sends);
    TEST_ASSERT_EQUAL_UINT(62, sleeps);
}

static void missing_network_disables_probe(void)
{
    startup_ok = 0;

    cd_lookup_open("192.0.2.1");
    cd_lookup_poll(&source);
    TEST_ASSERT_EQUAL_UINT(0, thread_creates);
    TEST_ASSERT_EQUAL_UINT(0, sends);
}

static void source_change_clears_probe(void)
{
    open_client();

    source.kind = AUDIO_SOURCE_NETWORK;

    cd_lookup_poll(&source);
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(0, sends);
}

static void restarted_client_uses_disc_identity(void)
{
    open_client();

    stop_after = 1;

    test_worker_run();

    unsigned old = request_token;

    TEST_ASSERT_TRUE(cd_lookup_close() == 0);
    ++disc.start[disc.count];

    sleeps = 0;

    open_client();
    test_worker_run();
    TEST_ASSERT_NOT_EQUAL(old, request_token);
}

static void correlation_script(void)
{
    if (sleeps == 1)
    {
        TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
        queue_reply(request_token, "{\"type\":\"metadata\",\"data\":{\"title\":\"First track\",\"artist\":\"Artist\",\"album\":\"Album\"}}");

        reply_peer.sin_port = 0;
    }
    else if (sleeps == 2)
    {
        TEST_ASSERT_EQUAL_STRING("", source.metadata.title);

        reply_peer   = service;
        reply_queued = 1;
    }
    else if (sleeps == 3)
    {
        TEST_ASSERT_EQUAL_STRING("First track", source.metadata.title);

        reply[reply_size - 1] = '!';
        reply_queued          = 1;
    }
    else if (sleeps == 4)
    {
        TEST_ASSERT_EQUAL_STRING("First track", source.metadata.title);

        old_token       = request_token;
        source.cd.track = 2;

        cd_lookup_poll(&source);
        cover_reply(old_token);
    }
    else if (sleeps == 5)
    {
        TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
        ++source.cd.generation;

        source.cd.track = 1;

        cd_lookup_poll(&source);
        cover_reply(old_token);
    }
    else if (sleeps == 6)
    {
        TEST_ASSERT_EQUAL_STRING("", source.metadata.album);

        source.kind = AUDIO_SOURCE_NETWORK;

        cd_lookup_poll(&source);
        cover_reply(request_token);
    }
    else
    {
        TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
    }
}

static void replies_are_correlated_and_parsed_atomically(void)
{
    reply_state = CD_LOOKUP_ARTWORK_PENDING;

    open_client();

    stop_after = 7;
    sleep_hook = correlation_script;

    test_worker_run();
}

static void disabled_or_invalid_host_is_harmless(void)
{
    cd_lookup_open("");
    cd_lookup_open("192.168.1.999");
    cd_lookup_poll(&source);
    TEST_ASSERT_EQUAL_UINT(0, thread_creates);
    TEST_ASSERT_EQUAL_UINT(0, sends);
}

static void track_change_script(void)
{
    if (sleeps == 10)
    {
        TEST_ASSERT_EQUAL_UINT(1, sends);

        source.cd.track = 2;

        cd_lookup_poll(&source);
    }
}

static void completed_lookup_stops_until_track_change(void)
{
    open_client();

    auto_reply       = 1;
    artwork_complete = 1;
    sleep_hook       = track_change_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(3, sends); /* Initial track, then two unanswered new-track attempts. */
}

static void unfinished_artwork_keeps_lookup_retries(void)
{
    open_client();

    auto_reply = 1;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(2, sends);
}

static void missing_cover_stops_until_track_change(void)
{
    reply_state = CD_LOOKUP_ARTWORK_UNAVAILABLE;

    completed_lookup_stops_until_track_change();
}

static void pending_cover_keeps_polling(void)
{
    reply_state = CD_LOOKUP_ARTWORK_PENDING;

    unfinished_artwork_keeps_lookup_retries();
}

static void invalid_artwork_state_is_rejected(void)
{
    reply_state = 255;

    open_client();

    auto_reply = 1;

    test_worker_run();
    TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
    TEST_ASSERT_EQUAL_UINT(2, sends);
}

static void eject_script(void)
{
    if (sleeps == 3)
    {
        TEST_ASSERT_EQUAL_STRING("Track", source.metadata.title);

        old_token         = request_token;
        source.cd.present = 0;

        cd_lookup_poll(&source);
    }
    else if (sleeps == 4)
    {
        TEST_ASSERT_EQUAL_UINT(1, sends);

        source.cd.present = 1;

        ++source.cd.generation;
        cd_lookup_poll(&source);
        cover_reply(old_token);
    }
    else if (sleeps == 5)
    {
        TEST_ASSERT_NOT_EQUAL(old_token, request_token);
        TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
        cover_reply(request_token);
    }
    else if (sleeps == 7)
    {
        TEST_ASSERT_EQUAL_STRING("Track", source.metadata.title);
    }
}

static void eject_and_reinsert_restarts_completed_lookup(void)
{
    open_client();

    auto_reply       = 1;
    artwork_complete = 1;
    sleep_hook       = eject_script;
    stop_after       = 9;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(2, sends);
}

static void eject_and_reinsert_restarts_missing_cover_lookup(void)
{
    reply_state = CD_LOOKUP_ARTWORK_UNAVAILABLE;

    eject_and_reinsert_restarts_completed_lookup();
}

static void failed_join_rejects_reopen(void)
{
    open_client();

    status_failure = 1;

    TEST_ASSERT_TRUE(!(cd_lookup_close() == 0));
    cd_lookup_open("192.0.2.2");
    TEST_ASSERT_EQUAL_UINT(1, test_worker_opens());
    TEST_ASSERT_EQUAL_UINT(0, closes);

    stop_after = 1;

    test_worker_run();
    TEST_ASSERT_EQUAL_HEX32(0xc0000201, ntohl(service.sin_addr.s_addr));

    status_failure = 0;

    TEST_ASSERT_TRUE(cd_lookup_close() == 0);
    TEST_ASSERT_EQUAL_UINT(1, closes);

    sleeps = 0;

    cd_lookup_open("192.0.2.2");
    cd_lookup_poll(&source);
    test_worker_run();
    TEST_ASSERT_EQUAL_HEX32(0xc0000202, ntohl(service.sin_addr.s_addr));
}

static void socket_close_failure_retains_ownership(void)
{
    open_client();

    network_close_ok = 0;

    TEST_ASSERT_TRUE(!(cd_lookup_close() == 0));
    cd_lookup_open("192.0.2.2");
    TEST_ASSERT_EQUAL_UINT(1, test_worker_opens());

    network_close_ok = 1;

    TEST_ASSERT_TRUE(cd_lookup_close() == 0);

    unsigned attempts = closes;

    TEST_ASSERT_TRUE(cd_lookup_close() == 0);
    TEST_ASSERT_EQUAL_UINT(attempts, closes);
    cd_lookup_open("192.0.2.2");
    TEST_ASSERT_EQUAL_UINT(2, test_worker_opens());
}

static void queue_busy(unsigned token, unsigned delay)
{
    /* Independent wire fixture: CDB1, big-endian token and delay. */
    memcpy(reply, "CDB1", 4);

    uint32_t wire = htonl(token);

    memcpy(reply + 4, &wire, 4);

    wire = htonl(delay);

    memcpy(reply + 8, &wire, 4);

    reply_size   = 12;
    reply_peer   = service;
    reply_queued = 1;
}

static void busy_script(void)
{
    if (sleeps == 1)
    {
        queue_busy(request_token, 15000);
    }

    if (sleeps == 150)
    {
        TEST_ASSERT_EQUAL_UINT(1, sends);
    }
}

static void busy_reply_defers_retry(void)
{
    open_client();

    sleep_hook = busy_script;
    stop_after = 152;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(2, sends);
    TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
}

static void busy_delay_survives_clock_wrap(void)
{
    clock_ms = UINT32_MAX - 1000;

    busy_reply_defers_retry();
}

static unsigned invalid_busy_case;

static void invalid_busy_script(void)
{
    if (sleeps != 1)
    {
        return;
    }

    queue_busy(request_token, 15000);
    switch (invalid_busy_case)
    {
    case 0:
        queue_busy(request_token + 1, 15000);
        break;
    case 1:
        reply_peer.sin_port = 0;

        break;
    case 2:
        reply_peer.sin_addr.s_addr = 0;

        break;
    case 3:
        reply_size = 11;

        break;
    case 4:
        queue_busy(request_token, 0);
        break;
    case 5:
        queue_busy(request_token, 60001);
        break;
    case 6:
        reply[0] = 'X';

        break;
    }
}

static void invalid_busy_replies_do_not_delay_retry(void)
{
    for (invalid_busy_case = 0; invalid_busy_case < 7; ++invalid_busy_case)
    {
        sleeps = sends = 0;

        open_client();

        sleep_hook = invalid_busy_script;

        test_worker_run();
        TEST_ASSERT_EQUAL_UINT(2, sends);
        TEST_ASSERT_EQUAL_INT(0, cd_lookup_close());
    }
}

static void busy_track_script(void)
{
    busy_script();

    if (sleeps == 10)
    {
        source.cd.track = 2;

        cd_lookup_poll(&source);
    }
}

static void track_change_clears_busy_delay(void)
{
    open_client();

    sleep_hook = busy_track_script;
    stop_after = 20;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(2, sends);
}

static void metadata_busy_script(void)
{
    if (sleeps == 1)
    {
        reply_state = CD_LOOKUP_ARTWORK_PENDING;

        cover_reply(request_token);
    }

    if (sleeps == 2)
    {
        queue_busy(request_token, 15000);
    }

    if (sleeps == 3)
    {
        TEST_ASSERT_EQUAL_STRING("Track", source.metadata.title);
    }

    if (sleeps == 4)
    {
        cover_reply(request_token);
    }
}

static void metadata_survives_busy_and_resets_delay(void)
{
    open_client();

    sleep_hook = metadata_busy_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(2, sends);
    TEST_ASSERT_EQUAL_STRING("Track", source.metadata.title);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(busy_reply_defers_retry);
    RUN_TEST(busy_delay_survives_clock_wrap);
    RUN_TEST(invalid_busy_replies_do_not_delay_retry);
    RUN_TEST(track_change_clears_busy_delay);
    RUN_TEST(metadata_survives_busy_and_resets_delay);
    RUN_TEST(failed_join_rejects_reopen);
    RUN_TEST(socket_close_failure_retains_ownership);
    RUN_TEST(missing_cover_stops_until_track_change);
    RUN_TEST(pending_cover_keeps_polling);
    RUN_TEST(invalid_artwork_state_is_rejected);
    RUN_TEST(eject_and_reinsert_restarts_missing_cover_lookup);
    RUN_TEST(disabled_or_invalid_host_is_harmless);
    RUN_TEST(eject_and_reinsert_restarts_completed_lookup);
    RUN_TEST(completed_lookup_stops_until_track_change);
    RUN_TEST(unfinished_artwork_keeps_lookup_retries);
    RUN_TEST(replies_are_correlated_and_parsed_atomically);
    RUN_TEST(restarted_client_uses_disc_identity);
    RUN_TEST(absent_service_is_harmless);
    RUN_TEST(missing_network_disables_probe);
    RUN_TEST(source_change_clears_probe);

    return UNITY_END();
}
