#include "recognition/client.h"
#include "recognition/protocol.h"
#include "audio/cd/cd.h"
#include "platform/network/runtime.h"
#include "platform/thread/scheduler.h"
#include "support/worker_driver.h"
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

#define ID           "R5ZoxkYIj.9IlgyFd.SaQ9dcXnM-"
#define RESULT       "/cd/v2/lookups/" ID
#define COVER        "/covers/" ID "-release.jpg"
#define PENDING      "{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"pending\",\"result_path\":\"" RESULT "\",\"retry_after_seconds\":5}"
#define MATCH_PREFIX "{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"matched\",\"track_count\":2,\"album\":\"Album\",\"artist\":\"Artist\",\"tracks\":[{\"number\":1,\"title\":\"First\",\"artist\":\"\"},{\"number\":2,\"title\":\"Second\",\"artist\":\"Guest\"}],\"artwork\":"
#define MATCH        MATCH_PREFIX "{\"status\":\"unavailable\"}}"
#define MATCH_ART    MATCH_PREFIX "{\"status\":\"pending\",\"retry_after_seconds\":5}}"
#define NO_MATCH     "{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"no_match\",\"expires_in_seconds\":86400}"
#define BUSY         "{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"error\",\"error\":{\"code\":\"busy\",\"message\":\"Busy\",\"retryable\":true},\"retry_after_seconds\":5}"
#define RATE_LIMITED "{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"error\",\"error\":{\"code\":\"rate_limited\",\"message\":\"Limited\",\"retryable\":true},\"retry_after_seconds\":5}"
#define MISSING      "{\"protocol\":2,\"status\":\"error\",\"error\":{\"code\":\"result_unavailable\",\"message\":\"Missing\",\"retryable\":false}}"

enum
{
    BODY_DELAY,
    HEADER_FALLBACK,
    IMAGE_ALWAYS_MISSING,
    IMAGE_TOO_WIDE,
    ART_LONG_PATH,
    PENDING_FOREVER,
    LONG_BUSY,
    IMAGE_MISSING,
    BAD_HEADERS,
    TRUNCATED,
    ART_OVERSIZED,
    CACHED,
    PENDING_MATCH,
    NO_RESPONSE,
    PROBE_DOWN,
    POST_LOST,
    ART_READY,
    ART_DOWN,
    NO_MATCH_MODE,
    RECOVERY_MISSING,
    RECOVERY_BUSY,
    RECOVERY_RATE_LIMITED,
    POST_BUSY,
    MALFORMED,
    ACCEPTED_BUSY,
    ART_BAD_IMAGE
};

int                      _gp;
static unsigned          clock_ms, sleeps, stop_after, probes, posts, gets, art_gets, images, sockets, socket_closes;
static unsigned          address_delay;
static int               startup_ok, network_close_ok, address_ready, link_ready, next_sema, mode, status_failure, fragment;
static int               pending_once, busy_once;
static CdToc             disc;
static AudioSourceStatus source;
static char              request[2048], wire[8192];
static unsigned          request_size, wire_size, wire_at;
static void (*sleep_hook)(void);
static const unsigned char jpeg[] = { 0xff, 0xd8, 0xff, 0xc0, 0, 11, 8, 0, 1, 0, 1, 1, 1, 0x11, 0, 0xff, 0xda, 0, 2, 0xff, 0xd9 };

/**
 * @brief Stage a framed fixture HTTP response with an explicit body length.
 */
static void reply_bytes(int status, const void* body, unsigned size, const char* headers, const char* type)
{
    int n = snprintf(wire, sizeof(wire), "HTTP/1.1 %d Test\r\nContent-Type: %s\r\nContent-Length: %u\r\n%s\r\n", status, type, size, headers);

    TEST_ASSERT_TRUE(n > 0 && (unsigned)n + size <= sizeof(wire));
    memcpy(wire + n, body, size);

    wire_size = (unsigned)n + size;
    wire_at   = 0;
}

/**
 * @brief Stage a fixture JSON HTTP response from a terminated body.
 */
static void reply(int status, const char* body, const char* headers)
{
    reply_bytes(status, body, (unsigned)strlen(body), headers, "application/json; charset=utf-8");
}

/**
 * @brief Intercept socket calls for fixture assertions and injected outcomes.
 */
int __wrap_socket(int domain, int type, int protocol)
{
    TEST_ASSERT_EQUAL_INT(AF_INET, domain);
    TEST_ASSERT_EQUAL_INT(SOCK_STREAM, type);
    (void)protocol;
    ++sockets;

    request_size = wire_size = wire_at = 0;

    return 10;
}

/**
 * @brief Intercept connect calls for fixture assertions and injected outcomes.
 */
int __wrap_connect(int fd, const struct sockaddr* address, socklen_t length)
{
    (void)length;
    TEST_ASSERT_EQUAL_INT(10, fd);

    const struct sockaddr_in* peer = (const struct sockaddr_in*)address;

    TEST_ASSERT_EQUAL_HEX32(0xc0000201, ntohl(peer->sin_addr.s_addr));
    TEST_ASSERT_EQUAL_UINT(12890, ntohs(peer->sin_port));

    errno = EINPROGRESS;

    return -1;
}

/**
 * @brief Intercept send calls for fixture assertions and injected outcomes.
 */
ssize_t __wrap_send(int fd, const void* data, size_t size, int flags)
{
    (void)fd;
    (void)flags;
    TEST_ASSERT_TRUE(request_size + size < sizeof(request));
    memcpy(request + request_size, data, size);

    request_size += (unsigned)size;
    request[request_size] = 0;

    if (strstr(request, "GET /cd/v2/service "))
    {
        ++probes;

        if (mode == NO_RESPONSE)
        {
            return (ssize_t)size;
        }

        if (mode == PROBE_DOWN)
        {
            reply(503, "{\"protocol\":2,\"status\":\"error\",\"error\":{\"code\":\"service_unavailable\",\"message\":\"Offline\",\"retryable\":true},\"retry_after_seconds\":5}", "Retry-After: 5\r\n");
        }
        else
        {
            reply(200, "{\"protocol\":2,\"service\":\"stroom-cd-recognition\",\"ready\":true}", "");
        }
    }
    else if (strstr(request, "POST /cd/v2/lookups "))
    {
        ++posts;
        TEST_ASSERT_NULL(strstr(request, "\"version\":"));
        TEST_ASSERT_NOT_NULL(strstr(request, "\"offsets\":[150,15363]"));
        TEST_ASSERT_NULL(strstr(request, "\"track\":"));
        TEST_ASSERT_NULL(strstr(request, "\"generation\":"));

        if (mode == LONG_BUSY)
        {
            reply(503, "{\"protocol\":2,\"status\":\"error\",\"error\":{\"code\":\"busy\",\"message\":\"Busy\",\"retryable\":true},\"retry_after_seconds\":604800}", "Retry-After: 604800\r\n");
        }
        else if (mode == BAD_HEADERS)
        {
            reply_bytes(200, MATCH, sizeof(MATCH) - 1, "", "text/plain");
        }
        else if (mode == TRUNCATED)
        {
            reply_bytes(200, MATCH, sizeof(MATCH), "", "application/json");
            --wire_size;
        }
        else if (mode == POST_LOST)
        {
            return (ssize_t)size;
        }
        else if (mode == HEADER_FALLBACK)
        {
            reply_bytes(503, "<html>Busy</html>", 17, "Retry-After: 604800\r\n", "text/html");
        }
        else if (mode == BODY_DELAY)
        {
            reply(202, PENDING, "Retry-After: 604800\r\n");
        }
        else if (mode == POST_BUSY)
        {
            reply(503, BUSY, "Retry-After: 5\r\n");
        }
        else if (mode == NO_MATCH_MODE)
        {
            reply(200, NO_MATCH, "");
        }
        else if (mode == PENDING_MATCH || mode == RECOVERY_MISSING || mode == RECOVERY_BUSY || mode == RECOVERY_RATE_LIMITED || mode == ACCEPTED_BUSY || mode == PENDING_FOREVER)
        {
            reply(202, PENDING, "Location: " RESULT "\r\nRetry-After: 5\r\n");
        }
        else if (mode == MALFORMED)
        {
            reply(200, "{\"protocol\":2,\"status\":\"matched\"}", "");
        }
        else
        {
            reply(200, mode == ART_READY || mode == ART_DOWN || mode == ART_BAD_IMAGE || mode == ART_OVERSIZED || mode == IMAGE_MISSING || mode == ART_LONG_PATH || mode == IMAGE_ALWAYS_MISSING || mode == IMAGE_TOO_WIDE ? MATCH_ART : MATCH, "");
        }
    }
    else if (strstr(request, "GET " RESULT " ") && (mode == ART_READY || mode == ART_DOWN || mode == ART_BAD_IMAGE || mode == ART_OVERSIZED || mode == IMAGE_MISSING || mode == ART_LONG_PATH || mode == IMAGE_ALWAYS_MISSING || mode == IMAGE_TOO_WIDE))
    {
        ++art_gets;

        if (mode == ART_DOWN)
        {
            return (ssize_t)size;
        }

        if (mode == ART_LONG_PATH)
        {
            char path[320], json[1024];

            snprintf(path, sizeof(path), "/covers/%0300d.jpg", 0);
            snprintf(json, sizeof(json), MATCH_PREFIX "{\"status\":\"available\",\"path\":\"%s\"}}", path);
            reply(200, json, "");
        }
        else
        {
            reply(200, MATCH_PREFIX "{\"status\":\"available\",\"path\":\"" COVER "\"}}", "");
        }
    }
    else if (strstr(request, "GET " RESULT " "))
    {
        int recovery = mode == RECOVERY_BUSY || mode == RECOVERY_RATE_LIMITED;

        ++gets;

        if ((mode == RECOVERY_MISSING || mode == RECOVERY_BUSY || mode == RECOVERY_RATE_LIMITED) && gets == 1)
        {
            return (ssize_t)size;
        }

        if (recovery && gets == 2)
        {
            reply(mode == RECOVERY_BUSY ? 503 : 429, mode == RECOVERY_BUSY ? BUSY : RATE_LIMITED, "Retry-After: 5\r\n");
        }
        else if (mode == RECOVERY_MISSING && gets == 2)
        {
            reply(404, MISSING, "");
        }
        else if (mode == ACCEPTED_BUSY && !busy_once++)
        {
            reply(503, BUSY, "Retry-After: 5\r\n");
        }
        else if (!recovery && (mode == PENDING_FOREVER || !pending_once++))
        {
            reply(202, PENDING, "");
        }
        else
        {
            reply(200, MATCH, "");
        }
    }
    else if (strstr(request, "GET " COVER " "))
    {
        ++images;

        if ((mode == IMAGE_MISSING && images == 1) || mode == IMAGE_ALWAYS_MISSING)
        {
            reply(404, MISSING, "");
        }
        else if (mode == ART_OVERSIZED)
        {
            wire_size = (unsigned)snprintf(wire, sizeof(wire), "HTTP/1.1 200 Test\r\nContent-Type: image/jpeg\r\nContent-Length: 524289\r\n\r\n");
            wire_at   = 0;
        }
        else if (mode == IMAGE_TOO_WIDE)
        {
            unsigned char large[sizeof(jpeg)];

            memcpy(large, jpeg, sizeof(large));

            large[9]  = 2;
            large[10] = 89; /* 601 pixels. */

            reply_bytes(200, large, sizeof(large), "", "image/jpeg");
        }
        else if (mode == ART_BAD_IMAGE)
        {
            reply_bytes(200, "broken", 6, "", "image/jpeg");
        }
        else
        {
            reply_bytes(200, jpeg, sizeof(jpeg), "", "image/jpeg");
        }
    }
    else
    {
        TEST_FAIL_MESSAGE(request);
    }

    return (ssize_t)size;
}

/**
 * @brief Intercept recv calls for fixture assertions and injected outcomes.
 */
ssize_t __wrap_recv(int fd, void* data, size_t size, int flags)
{
    (void)fd;
    (void)flags;

    if (wire_at == wire_size)
    {
        if (wire_size && mode == TRUNCATED)
        {
            return 0;
        }

        errno = EWOULDBLOCK;

        return -1;
    }

    unsigned n = wire_size - wire_at;

    if (n > size)
    {
        n = (unsigned)size;
    }

    if (fragment && n > (unsigned)fragment)
    {
        n = (unsigned)fragment;
    }

    memcpy(data, wire + wire_at, n);

    wire_at += n;

    return n;
}

/**
 * @brief Intercept close calls for fixture assertions and injected outcomes.
 */
int __wrap_close(int fd)
{
    TEST_ASSERT_EQUAL_INT(10, fd);
    ++socket_closes;

    return 0;
}

/**
 * @brief Substitute _ps2sdk_ioctl in this host fixture.
 */
int _ps2sdk_ioctl(int fd, int command, void* value)
{
    (void)fd;
    TEST_ASSERT_EQUAL_INT(FIONBIO, command);
    TEST_ASSERT_EQUAL_UINT(1, *(unsigned long*)value);

    return 0;
}

/**
 * @brief Substitute platform_network_startup in this host fixture.
 */
int platform_network_startup(char* error, size_t capacity)
{
    snprintf(error, capacity, "offline");

    return startup_ok ? 0 : -1;
}

/**
 * @brief Substitute platform_network_ready_address in this host fixture.
 */
void platform_network_ready_address(char* address, size_t capacity)
{
    clock_ms += address_delay;
    address_delay = 0;

    snprintf(address, capacity, "%s", address_ready && link_ready ? "192.0.2.2" : "");
}

/**
 * @brief Substitute platform_network_close in this host fixture.
 */
int platform_network_close(void)
{
    return network_close_ok ? 0 : -1;
}

/**
 * @brief Substitute platform_millis in this host fixture.
 */
uint32_t platform_millis(void)
{
    return clock_ms;
}

/**
 * @brief Substitute cd_copy_toc in this host fixture.
 */
int cd_copy_toc(unsigned generation, CdToc* toc)
{
    if (!source.cd.present || source.cd.generation != generation)
    {
        return -1;
    }

    *toc = disc;

    return 0;
}

/**
 * @brief Substitute platform_sleep_us in this host fixture.
 */
void platform_sleep_us(uint32_t duration)
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
}

/**
 * @brief Substitute CreateSema in this host fixture.
 */
int CreateSema(ee_sema_t* sema)
{
    (void)sema;

    return ++next_sema;
}

/**
 * @brief Substitute WaitSema in this host fixture.
 */
int WaitSema(int id)
{
    (void)id;

    return 0;
}

/**
 * @brief Substitute SignalSema in this host fixture.
 */
int SignalSema(int id)
{
    (void)id;

    return 0;
}

/**
 * @brief Substitute DeleteSema in this host fixture.
 */
int DeleteSema(int id)
{
    (void)id;

    return 0;
}

/**
 * @brief Substitute CreateThread in this host fixture.
 */
int CreateThread(ee_thread_t* thread)
{
    TEST_ASSERT_NOT_NULL(thread->func);

    return 1;
}

/**
 * @brief Substitute StartThread in this host fixture.
 */
int StartThread(int id, void* arg)
{
    (void)id;
    (void)arg;

    return 0;
}

/**
 * @brief Substitute DeleteThread in this host fixture.
 */
int DeleteThread(int id)
{
    (void)id;

    return 0;
}

/**
 * @brief Substitute ExitThread in this host fixture.
 */
void ExitThread(void)
{}

/**
 * @brief Substitute ReferThreadStatus in this host fixture.
 */
int ReferThreadStatus(int id, ee_thread_status_t* status)
{
    (void)id;

    status->status = THS_DORMANT;

    return status_failure ? -1 : 0;
}

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    test_worker_reset();

    clock_ms = sleeps = probes = posts = gets = art_gets = images = sockets = socket_closes = 0;
    address_delay                                                                           = 0;
    startup_ok = network_close_ok = address_ready = link_ready = 1;
    next_sema = status_failure = fragment = pending_once = busy_once = 0;
    mode                                                             = CACHED;
    stop_after                                                       = 200;
    sleep_hook                                                       = NULL;
    disc                                                             = (CdToc){ .count = 2, .start = { 0, 15213, 32164 } };
    source                                                           = (AudioSourceStatus){ .kind = AUDIO_SOURCE_CD, .cd = { .present = 1, .generation = 1, .track = 1 } };
}

/**
 * @brief Finish fixture cleanup and resource checks after each test.
 */
void tearDown(void)
{
    status_failure   = 0;
    network_close_ok = 1;

    TEST_ASSERT_EQUAL_INT(0, cd_lookup_close());
    TEST_ASSERT_EQUAL_UINT(sockets, socket_closes);
}

/**
 * @brief Open the recognition client with the fixture service address.
 */
static void open_client(void)
{
    cd_lookup_open("192.0.2.1");
    cd_lookup_poll(&source);
}

/**
 * @brief Publish a track-change retry event for the inserted fixture disc.
 */
static void change_track(void)
{
    source.cd.track = source.cd.track == 1 ? 2 : 1;

    cd_lookup_poll(&source);
}

/**
 * @brief Advance recognition and change tracks in the scripted worker loop.
 */
static void track_script(void)
{
    if (sleeps == 100)
    {
        change_track();
    }
}

/**
 * @brief Verify cached disc is local across tracks.
 */
static void cached_disc_is_local_across_tracks(void)
{
    open_client();

    sleep_hook = track_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, probes);
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(0, gets);
    TEST_ASSERT_EQUAL_STRING("Second", source.metadata.title);
    TEST_ASSERT_EQUAL_STRING("Guest", source.metadata.artist);
}

/**
 * @brief Verify accepted lookup is polled without posting again.
 */
static void accepted_lookup_is_polled_without_posting_again(void)
{
    mode = PENDING_MATCH;

    open_client();

    sleep_hook = track_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(2, gets);
    TEST_ASSERT_EQUAL_STRING("Second", source.metadata.title);
}

/**
 * @brief Verify accepted busy keeps handle.
 */
static void accepted_busy_keeps_handle(void)
{
    mode       = ACCEPTED_BUSY;
    stop_after = 260;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(3, gets);
    TEST_ASSERT_EQUAL_STRING("First", source.metadata.title);
}

/**
 * @brief Verify no match does not retry on track change.
 */
static void no_match_does_not_retry_on_track_change(void)
{
    mode = NO_MATCH_MODE;

    open_client();

    sleep_hook = track_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
}

/**
 * @brief Verify failed probe sends no toc.
 */
static void failed_probe_sends_no_toc(void)
{
    mode = PROBE_DOWN;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, probes);
    TEST_ASSERT_EQUAL_UINT(0, posts);
}

/**
 * @brief Schedule a track change after a failed recognition attempt.
 */
static void retry_script(void)
{
    if (sleeps == 100)
    {
        mode = CACHED;

        change_track();
    }
}

/**
 * @brief Verify transport failure waits for track event.
 */
static void transport_failure_waits_for_track_event(void)
{
    mode = POST_LOST;

    open_client();

    sleep_hook = retry_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(2, probes);
    TEST_ASSERT_EQUAL_UINT(2, posts);
    TEST_ASSERT_EQUAL_STRING("Second", source.metadata.title);
}

/**
 * @brief Verify absent service has no timed probe loop.
 */
static void absent_service_has_no_timed_probe_loop(void)
{
    mode = NO_RESPONSE;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, probes);
    TEST_ASSERT_EQUAL_UINT(0, posts);
}

/**
 * @brief Publish a usable DHCP address during the recognition worker script.
 */
static void dhcp_script(void)
{
    if (sleeps == 50)
    {
        address_ready = 1;
    }
}

/**
 * @brief Verify late dhcp triggers initial probe.
 */
static void late_dhcp_triggers_initial_probe(void)
{
    address_ready = 0;

    open_client();

    sleep_hook = dhcp_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, probes);
    TEST_ASSERT_EQUAL_UINT(1, posts);
}

/**
 * @brief Verify slow address query does not expire initial probe.
 */
static void slow_address_query_does_not_expire_initial_probe(void)
{
    address_delay = 4000;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, probes);
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_STRING("First", source.metadata.title);
}

/**
 * @brief Publish Ethernet carrier during the recognition worker script.
 */
static void link_script(void)
{
    if (sleeps == 50)
    {
        TEST_ASSERT_EQUAL_UINT(0, sockets);

        link_ready = 1;
    }
}

/**
 * @brief Verify assigned ip waits for link before initial probe.
 */
static void assigned_ip_waits_for_link_before_initial_probe(void)
{
    link_ready = 0;

    open_client();

    sleep_hook = link_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, probes);
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_STRING("First", source.metadata.title);
}

/**
 * @brief Verify busy post with id is not acceptance.
 */
static void busy_post_with_id_is_not_acceptance(void)
{
    mode = POST_BUSY;

    open_client();

    sleep_hook = retry_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, probes);
    TEST_ASSERT_EQUAL_UINT(2, posts);
    TEST_ASSERT_EQUAL_UINT(0, gets);
}

/**
 * @brief Drive a suspended lookup through its recovery event.
 */
static void recovery_script(void)
{
    if (sleeps == 120)
    {
        change_track();
    }
}

/**
 * @brief Verify lost result recovers get first then one post.
 */
static void lost_result_recovers_get_first_then_one_post(void)
{
    mode       = RECOVERY_MISSING;
    stop_after = 300;

    open_client();

    sleep_hook = recovery_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(2, probes);
    TEST_ASSERT_EQUAL_UINT(2, posts);
    TEST_ASSERT_EQUAL_UINT(4, gets);
    TEST_ASSERT_EQUAL_STRING("Second", source.metadata.title);
}

/**
 * @brief Drive lookup recovery while the service advertises a cooldown.
 */
static void recovery_throttled_script(void)
{
    if (sleeps == 120 || sleeps == 130 || sleeps == 240)
    {
        change_track();
    }

    if (sleeps == 200)
    {
        /* The cooldown has expired, but no later retry event has occurred. */
        TEST_ASSERT_EQUAL_UINT(2, gets);
        TEST_ASSERT_EQUAL_UINT(1, posts);
        TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
    }
}

/**
 * @brief Check recovery for the requested rate-limit or busy response.
 */
static void recovery_throttled(int response_mode)
{
    mode       = response_mode;
    stop_after = 300;

    open_client();

    sleep_hook = recovery_throttled_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(2, probes);
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(3, gets);
    TEST_ASSERT_EQUAL_STRING("Second", source.metadata.title);
}

/**
 * @brief Verify recovery busy keeps handle until next event.
 */
static void recovery_busy_keeps_handle_until_next_event(void)
{
    recovery_throttled(RECOVERY_BUSY);
}

/**
 * @brief Verify recovery rate limited keeps handle until next event.
 */
static void recovery_rate_limited_keeps_handle_until_next_event(void)
{
    recovery_throttled(RECOVERY_RATE_LIMITED);
}

/**
 * @brief Verify artwork does not repeat recognition.
 */
static void artwork_does_not_repeat_recognition(void)
{
    mode = ART_READY;

    open_client();

    sleep_hook = track_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(1, art_gets);
    TEST_ASSERT_EQUAL_UINT(1, images);

    ArtworkBlob blob = { 0 };

    TEST_ASSERT_EQUAL_INT(0, cd_lookup_take_artwork(&source.metadata, &blob));
    TEST_ASSERT_EQUAL_UINT(sizeof(jpeg), blob.size);
    TEST_ASSERT_EQUAL_STRING("Second", blob.metadata.title);
    free(blob.data);
}

/**
 * @brief Verify artwork failures suspend after three requests.
 */
static void artwork_failures_suspend_after_three_requests(void)
{
    mode       = ART_DOWN;
    stop_after = 500;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(3, art_gets);
    TEST_ASSERT_EQUAL_STRING("First", source.metadata.title);
}

/**
 * @brief Drive the client through a rejected cover-image response.
 */
static void bad_image_script(void)
{
    if (sleeps == 120 || sleeps == 180)
    {
        change_track();
    }
}

/**
 * @brief Verify malformed image path is not downloaded again.
 */
static void malformed_image_path_is_not_downloaded_again(void)
{
    mode = ART_BAD_IMAGE;

    open_client();

    sleep_hook = bad_image_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(1, images);
    TEST_ASSERT_EQUAL_UINT(3, art_gets);
}

/**
 * @brief Remove the fixture disc during an active recognition exchange.
 */
static void eject_script(void)
{
    if (sleeps == 50)
    {
        source.cd.present = 0;

        cd_lookup_poll(&source);
    }

    if (sleeps == 100)
    {
        source.cd.present = 1;

        ++source.cd.generation;
        cd_lookup_poll(&source);
    }
}

/**
 * @brief Verify reinsertion starts new lookup.
 */
static void reinsertion_starts_new_lookup(void)
{
    open_client();

    sleep_hook = eject_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, probes);
    TEST_ASSERT_EQUAL_UINT(2, posts);
}

/**
 * @brief Verify fragmented response is supported.
 */
static void fragmented_response_is_supported(void)
{
    fragment   = 17;
    stop_after = 300;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_STRING("First", source.metadata.title);
}

/**
 * @brief Verify malformed metadata does not publish.
 */
static void malformed_metadata_does_not_publish(void)
{
    mode = MALFORMED;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
}

/**
 * @brief Verify disabled or missing network is harmless.
 */
static void disabled_or_missing_network_is_harmless(void)
{
    cd_lookup_open("");
    cd_lookup_open("192.168.1.999");
    TEST_ASSERT_EQUAL_UINT(0, test_worker_opens());

    startup_ok = 0;

    cd_lookup_open("192.0.2.1");
    TEST_ASSERT_EQUAL_UINT(0, test_worker_opens());
}

/**
 * @brief Verify cleanup failure prevents reopen.
 */
static void cleanup_failure_prevents_reopen(void)
{
    open_client();

    status_failure = 1;

    TEST_ASSERT_NOT_EQUAL(0, cd_lookup_close());
    cd_lookup_open("192.0.2.1");
    TEST_ASSERT_EQUAL_UINT(1, test_worker_opens());

    status_failure = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_lookup_close());

    network_close_ok = 0;

    cd_lookup_open("192.0.2.1");
    TEST_ASSERT_NOT_EQUAL(0, cd_lookup_close());
    cd_lookup_open("192.0.2.1");
    TEST_ASSERT_EQUAL_UINT(2, test_worker_opens());
}

/**
 * @brief Publish a retry event before its cooldown has expired.
 */
static void early_event_script(void)
{
    if (sleeps == 20)
    {
        mode = CACHED;

        change_track();
    }
}

/**
 * @brief Verify early track event does not queue a delayed post.
 */
static void early_track_event_does_not_queue_a_delayed_post(void)
{
    mode = POST_BUSY;

    open_client();

    sleep_hook = early_event_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
}

/**
 * @brief Verify long busy survives track changes and clock wrap.
 */
static void long_busy_survives_track_changes_and_clock_wrap(void)
{
    mode     = LONG_BUSY;
    clock_ms = 0xfffff000u;

    open_client();

    sleep_hook = track_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(1, probes);
}

/**
 * @brief Advance the fixture clock through the lookup waiting deadline.
 */
static void deadline_script(void)
{
    if (sleeps == 100)
    {
        clock_ms += 300000;
    }

    if (sleeps == 120)
    {
        TEST_ASSERT_EQUAL_UINT(1, posts);
        TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
    }

    if (sleeps == 130)
    {
        mode = CACHED;

        change_track();
    }
}

/**
 * @brief Verify accepted waiting deadline recovers known handle.
 */
static void accepted_waiting_deadline_recovers_known_handle(void)
{
    mode       = PENDING_FOREVER;
    stop_after = 300;

    open_client();

    sleep_hook = deadline_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(3, gets);
    TEST_ASSERT_EQUAL_STRING("Second", source.metadata.title);
}

/**
 * @brief Verify unsupported http headers are rejected.
 */
static void unsupported_http_headers_are_rejected(void)
{
    mode = BAD_HEADERS;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
}

/**
 * @brief Verify truncated http body is rejected.
 */
static void truncated_http_body_is_rejected(void)
{
    mode = TRUNCATED;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_STRING("", source.metadata.title);
}

/**
 * @brief Verify oversized image path is not downloaded again.
 */
static void oversized_image_path_is_not_downloaded_again(void)
{
    mode = ART_OVERSIZED;

    open_client();

    sleep_hook = bad_image_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, images);
    TEST_ASSERT_EQUAL_UINT(3, art_gets);
    TEST_ASSERT_EQUAL_UINT(1, posts);
}

/**
 * @brief Verify missing image checks status without resubmitting toc.
 */
static void missing_image_checks_status_without_resubmitting_toc(void)
{
    mode = IMAGE_MISSING;

    open_client();

    stop_after = 400;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(2, art_gets);
    TEST_ASSERT_EQUAL_UINT(2, images);
}

/**
 * @brief Drive artwork failures and a later track-change retry.
 */
static void artwork_retry_script(void)
{
    if (sleeps == 450)
    {
        mode = ART_READY;

        change_track();
    }
}

/**
 * @brief Verify track change resumes only suspended artwork.
 */
static void track_change_resumes_only_suspended_artwork(void)
{
    mode = ART_DOWN;

    open_client();

    sleep_hook = artwork_retry_script;
    stop_after = 500;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(4, art_gets);
    TEST_ASSERT_EQUAL_UINT(1, images);
}

/**
 * @brief Inject disc replacement during a scripted worker iteration.
 */
static void replacement_script(void)
{
    if (sleeps == 20)
    {
        mode = CACHED;

        ++source.cd.generation;
        cd_lookup_poll(&source);
    }
}

/**
 * @brief Verify disc replacement cancels an inflight exchange.
 */
static void disc_replacement_cancels_an_inflight_exchange(void)
{
    mode = POST_LOST;

    open_client();

    sleep_hook = replacement_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(2, posts);
    TEST_ASSERT_EQUAL_STRING("First", source.metadata.title);
}

/**
 * @brief Verify resolved image url is bounded before download.
 */
static void resolved_image_url_is_bounded_before_download(void)
{
    mode = ART_LONG_PATH;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(0, images);
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_STRING("First", source.metadata.title);
}

/**
 * @brief Verify json delay overrides header and get needs no location.
 */
static void json_delay_overrides_header_and_get_needs_no_location(void)
{
    mode = BODY_DELAY;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(2, gets);
    TEST_ASSERT_EQUAL_STRING("First", source.metadata.title);
}

/**
 * @brief Verify proxy error retains header cooldown.
 */
static void proxy_error_retains_header_cooldown(void)
{
    mode = HEADER_FALLBACK;

    open_client();

    sleep_hook = track_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(1, probes);
}

/**
 * @brief Verify successful status does not reset failed image budget.
 */
static void successful_status_does_not_reset_failed_image_budget(void)
{
    mode = IMAGE_ALWAYS_MISSING;

    open_client();
    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, posts);
    TEST_ASSERT_EQUAL_UINT(3, images);
    TEST_ASSERT_EQUAL_UINT(3, art_gets);
    TEST_ASSERT_EQUAL_STRING("First", source.metadata.title);
}

/**
 * @brief Verify image over 600 pixels is rejected for this insertion.
 */
static void image_over_600_pixels_is_rejected_for_this_insertion(void)
{
    mode = IMAGE_TOO_WIDE;

    open_client();

    sleep_hook = bad_image_script;

    test_worker_run();
    TEST_ASSERT_EQUAL_UINT(1, images);
    TEST_ASSERT_EQUAL_UINT(1, posts);
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(json_delay_overrides_header_and_get_needs_no_location);
    RUN_TEST(proxy_error_retains_header_cooldown);
    RUN_TEST(successful_status_does_not_reset_failed_image_budget);
    RUN_TEST(image_over_600_pixels_is_rejected_for_this_insertion);
    RUN_TEST(cached_disc_is_local_across_tracks);
    RUN_TEST(accepted_lookup_is_polled_without_posting_again);
    RUN_TEST(accepted_busy_keeps_handle);
    RUN_TEST(no_match_does_not_retry_on_track_change);
    RUN_TEST(failed_probe_sends_no_toc);
    RUN_TEST(transport_failure_waits_for_track_event);
    RUN_TEST(absent_service_has_no_timed_probe_loop);
    RUN_TEST(late_dhcp_triggers_initial_probe);
    RUN_TEST(slow_address_query_does_not_expire_initial_probe);
    RUN_TEST(assigned_ip_waits_for_link_before_initial_probe);
    RUN_TEST(busy_post_with_id_is_not_acceptance);
    RUN_TEST(lost_result_recovers_get_first_then_one_post);
    RUN_TEST(recovery_busy_keeps_handle_until_next_event);
    RUN_TEST(recovery_rate_limited_keeps_handle_until_next_event);
    RUN_TEST(artwork_does_not_repeat_recognition);
    RUN_TEST(artwork_failures_suspend_after_three_requests);
    RUN_TEST(malformed_image_path_is_not_downloaded_again);
    RUN_TEST(reinsertion_starts_new_lookup);
    RUN_TEST(fragmented_response_is_supported);
    RUN_TEST(malformed_metadata_does_not_publish);
    RUN_TEST(disabled_or_missing_network_is_harmless);
    RUN_TEST(cleanup_failure_prevents_reopen);
    RUN_TEST(early_track_event_does_not_queue_a_delayed_post);
    RUN_TEST(long_busy_survives_track_changes_and_clock_wrap);
    RUN_TEST(accepted_waiting_deadline_recovers_known_handle);
    RUN_TEST(unsupported_http_headers_are_rejected);
    RUN_TEST(truncated_http_body_is_rejected);
    RUN_TEST(oversized_image_path_is_not_downloaded_again);
    RUN_TEST(missing_image_checks_status_without_resubmitting_toc);
    RUN_TEST(track_change_resumes_only_suspended_artwork);
    RUN_TEST(disc_replacement_cancels_an_inflight_exchange);
    RUN_TEST(resolved_image_url_is_bounded_before_download);

    return UNITY_END();
}
