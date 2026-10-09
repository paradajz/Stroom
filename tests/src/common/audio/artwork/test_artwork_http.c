#include "audio/artwork/receiver.h"
#include "unity.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static uint8_t     image[ARTWORK_MAX_BYTES];
static ArtworkHttp http;
static uint32_t    observation_clock;
static unsigned    clock_step = 1;
#if STROOM_DIAGNOSTICS
static unsigned observed_phases[DIAGNOSTIC_ARTWORK_SOCKET_ERROR + 1];

/** @brief Exercise timing wrap and ensure the sink cannot corrupt socket errno. */
static void observe_transfer(const TrackMetadata* metadata, ArtworkObservation observation)
{
    TEST_ASSERT_TRUE(metadata->artwork_url[0] != 0);
    TEST_ASSERT_EQUAL_UINT32(1, observation.end - observation.begin);
    TEST_ASSERT_TRUE(observation.phase > 0 && observation.phase <= DIAGNOSTIC_ARTWORK_SOCKET_ERROR);
    ++observed_phases[observation.phase];

    errno = EDOM;
}
#endif

uint32_t platform_millis(void)
{
    uint32_t now = observation_clock;

    observation_clock += clock_step;

    return now;
}

/**
 * @brief Reset response framing for each case.
 */
void setUp(void)
{
    memset(&http, 0, sizeof(http));
    signal(SIGPIPE, SIG_IGN);
}

/**
 * @brief Fixtures release their own sockets.
 */
void tearDown(void)
{}

/**
 * @brief Decode a response one byte at a time to exercise all boundaries.
 * @param text Wire response.
 * @return Nonzero if every byte was accepted.
 */
static int feed(const char* text)
{
    while (*text)
    {
        if (artwork_http_feed(&http, text++, 1, image) < 0)
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Numeric addresses, dynamic ports and query versions form safe requests.
 */
static void urls(void)
{
    ArtworkUrl url;

    TEST_ASSERT_TRUE(artwork_url_parse("http://10.42.0.73:38149/artwork.jpg?v=123", &url) == 0);
    TEST_ASSERT_EQUAL_STRING("10.42.0.73", url.host);
    TEST_ASSERT_EQUAL_HEX32(0x0a2a0049, url.address);
    TEST_ASSERT_EQUAL_UINT(38149, url.port);
    TEST_ASSERT_EQUAL_STRING("/artwork.jpg?v=123", url.path);
    TEST_ASSERT_TRUE(artwork_url_parse("http://192.168.1.33", &url) == 0);
    TEST_ASSERT_EQUAL_STRING("/", url.path);
    TEST_ASSERT_EQUAL_UINT(80, url.port);

    const char* bad[] = { "https://1.2.3.4/a", "http://phone/a", "http://1.2.3.256/a", "http://1.2.3.4:0/a", "http://1.2.3.4:65536/a", "http://1.2.3.4/\r\nHeader:x", "http://1.2.3.4/a b", "http://user@1.2.3.4/a", "http://1.2.3.4/a#fragment", "http://1.2.3", "http://1.2.3.4extra/a" };

    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        TEST_ASSERT_TRUE_MESSAGE(!(artwork_url_parse(bad[i], &url) == 0), bad[i]);
    }
}

/**
 * @brief All supported body framings retain exactly the image bytes.
 */
static void bodies(void)
{
    TEST_ASSERT_TRUE(feed("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: 5\r\n\r\ncover"));
    TEST_ASSERT_TRUE(http.done);
    TEST_ASSERT_EQUAL_UINT(5, http.used);
    TEST_ASSERT_EQUAL_MEMORY("cover", image, 5);
    memset(&http, 0, sizeof(http));
    TEST_ASSERT_TRUE(feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nco\r\n3;foo=bar\r\nver\r\n0\r\nX-End: yes\r\n\r\n"));
    TEST_ASSERT_TRUE(http.done);
    TEST_ASSERT_EQUAL_MEMORY("cover", image, 5);
    memset(&http, 0, sizeof(http));
    TEST_ASSERT_TRUE(feed("HTTP/1.0 200 OK\r\n\r\ncover"));
    TEST_ASSERT_FALSE(http.done);
    TEST_ASSERT_EQUAL_INT(0, artwork_http_eof(&http));
    TEST_ASSERT_EQUAL_MEMORY("cover", image, 5);
}

/**
 * @brief Reject truncated, ambiguous, oversized and unsuccessful HTTP responses.
 */
static void bad_responses(void)
{
    const char* bad[] = { "HTTP/1.1 404 Missing\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 9999999999999\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nxyz\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n200001\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na!" };

    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        memset(&http, 0, sizeof(http));
        TEST_ASSERT_FALSE_MESSAGE(feed(bad[i]), bad[i]);
    }

    memset(&http, 0, sizeof(http));
    TEST_ASSERT_FALSE(feed("HTTP/1.1 200 OK\r\nContent-Length: 2097153\r\n\r\n"));
    TEST_ASSERT_TRUE(http.too_large);
    memset(&http, 0, sizeof(http));
    TEST_ASSERT_FALSE(feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n200001\r\n"));
    TEST_ASSERT_TRUE(http.too_large);
    memset(&http, 0, sizeof(http));
    TEST_ASSERT_TRUE(feed("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort"));
    TEST_ASSERT_LESS_THAN_INT(0, artwork_http_eof(&http));
}

/**
 * @brief Configure real loopback sockets without blocking.
 * @param fd Socket.
 * @return Zero on success, negative on failure, matching platform_socket_nonblocking.
 */
static int nonblocking(int fd)
{
    int yes = 1;

    return ioctl(fd, FIONBIO, &yes);
}

/**
 * @brief Exercise HTTP fetching, pointer handoff, cancellation and URL-version changes.
 */
static void fetch(void)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);

    if (listener < 0)
    {
        TEST_FAIL_MESSAGE("Cannot create artwork fixture listener");
        return;
    }

    struct sockaddr_in address = { 0 };

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    TEST_ASSERT_EQUAL_INT(0, bind(listener, (struct sockaddr*)&address, sizeof(address)));
    TEST_ASSERT_EQUAL_INT(0, listen(listener, 1));

    socklen_t length = sizeof(address);

    TEST_ASSERT_EQUAL_INT(0, getsockname(listener, (struct sockaddr*)&address, &length));
    TEST_ASSERT_EQUAL_INT(0, nonblocking(listener));

    char url[128];

    snprintf(url, sizeof(url), "http://127.0.0.1:%u/artwork.jpg?v=1", ntohs(address.sin_port));

    TrackMetadata metadata = { 0 };

    strcpy(metadata.artwork_url, url);
    strcpy(metadata.title, "First track");

    ArtworkReceiver receiver = { 0 };
#if STROOM_DIAGNOSTICS
    observation_clock = UINT32_MAX - 3;

    memset(observed_phases, 0, sizeof(observed_phases));

    receiver.observer = observe_transfer;
#endif
    ArtworkBlob blob = { 0 };
    uint32_t    now  = 100;

    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now, nonblocking, 1);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
    TEST_ASSERT_EQUAL_UINT(0, receiver.attempts);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 250, nonblocking, 1);

    int peer = accept(listener, NULL, NULL);

    if (peer < 0)
    {
        artwork_receiver_close(&receiver);
        close(listener);
        TEST_FAIL_MESSAGE("Artwork client did not connect");
        return;
    }

    TEST_ASSERT_EQUAL_INT(0, nonblocking(peer));
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 10, nonblocking, 1);

    char request[512];
    int  got = (int)recv(peer, request, sizeof(request) - 1, 0);

    TEST_ASSERT_GREATER_THAN_INT(0, got);

    request[got] = 0;

    char expected[512];

    snprintf(expected, sizeof(expected), "GET /artwork.jpg?v=1 HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nConnection: close\r\nAccept: image/jpeg, image/png\r\nAccept-Encoding: identity\r\n\r\n", ntohs(address.sin_port));
    TEST_ASSERT_EQUAL_STRING(expected, request);

    const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\ncover";

    TEST_ASSERT_EQUAL_INT(sizeof(response) - 1, send(peer, response, sizeof(response) - 1, 0));
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 10, nonblocking, 1);
    TEST_ASSERT_TRUE(artwork_receiver_take(&receiver, &blob) == 0);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_PTR(observe_transfer, receiver.observer);

    const ArtworkPhase required[] = { DIAGNOSTIC_ARTWORK_SOCKET, DIAGNOSTIC_ARTWORK_NONBLOCKING, DIAGNOSTIC_ARTWORK_CONNECT, DIAGNOSTIC_ARTWORK_SEND, DIAGNOSTIC_ARTWORK_RECEIVE, DIAGNOSTIC_ARTWORK_GROW, DIAGNOSTIC_ARTWORK_PARSE, DIAGNOSTIC_ARTWORK_CLOSE };

    for (unsigned i = 0; i < sizeof(required) / sizeof(*required); ++i)
    {
        TEST_ASSERT_GREATER_THAN_UINT(0, observed_phases[required[i]]);
    }
#endif
    TEST_ASSERT_EQUAL_STRING(metadata.artwork_url, blob.metadata.artwork_url);
    TEST_ASSERT_EQUAL_UINT(5, blob.size);
    TEST_ASSERT_EQUAL_MEMORY("cover", blob.data, 5);
    free(blob.data);
    TEST_ASSERT_TRUE(!(artwork_receiver_take(&receiver, &blob) == 0));
    close(peer);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 10, nonblocking, 1);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
    TEST_ASSERT_NULL(receiver.blob.data);

    /* CD label changes keep a stable cover URL cached. */
    strcpy(metadata.title, "CD next track");
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_URL, now += 10, nonblocking, 1);
    TEST_ASSERT_TRUE(receiver.complete);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
    TEST_ASSERT_EQUAL_UINT(1, receiver.attempts);

    /* Aria reuses the URL: title/artist changes must fetch fresh bytes. */
    strcpy(metadata.title, "Second track");
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 10, nonblocking, 1);
    TEST_ASSERT_NULL(receiver.blob.data);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
    strcpy(metadata.artist, "Second artist");
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 100, nonblocking, 1);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 249, nonblocking, 1);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
    TEST_ASSERT_EQUAL_UINT(0, receiver.attempts);
    TEST_ASSERT_FALSE(receiver.complete);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, ++now, nonblocking, 1);
    TEST_ASSERT_EQUAL_UINT(1, receiver.attempts);

    peer = accept(listener, NULL, NULL);

    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, peer);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 10, nonblocking, 1);

    char second[128];
    int  header_size = snprintf(second, sizeof(second), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", ARTWORK_MAX_BYTES);

    TEST_ASSERT_EQUAL_INT(header_size, send(peer, second, header_size, 0));
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 10, nonblocking, 1);
    TEST_ASSERT_LESS_THAN_UINT(ARTWORK_MAX_BYTES, receiver.capacity);
    memset(image, 77, sizeof(image));

    for (unsigned sent = 0; sent < ARTWORK_MAX_BYTES; sent += 2048)
    {
        TEST_ASSERT_EQUAL_INT(2048, send(peer, image + sent, 2048, 0));
        artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 10, nonblocking, 1);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT(receiver.http.used, receiver.capacity);
        TEST_ASSERT_LESS_OR_EQUAL_UINT(ARTWORK_MAX_BYTES, receiver.capacity);
    }

    TEST_ASSERT_TRUE(receiver.complete);
    TEST_ASSERT_TRUE(artwork_receiver_take(&receiver, &blob) == 0);
    TEST_ASSERT_EQUAL_STRING("Second track", blob.metadata.title);
    TEST_ASSERT_EQUAL_STRING(metadata.artwork_url, blob.metadata.artwork_url);
    TEST_ASSERT_EQUAL_UINT(ARTWORK_MAX_BYTES, blob.size);
    TEST_ASSERT_EQUAL_MEMORY(image, blob.data, ARTWORK_MAX_BYTES);
    TEST_ASSERT_EQUAL_UINT(0, receiver.capacity);
    free(blob.data);
    close(peer);
    strcat(metadata.artwork_url, "2");
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 10, nonblocking, 1);
    TEST_ASSERT_NULL(receiver.blob.data);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 250, nonblocking, 1);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, now += 180000, nonblocking, 1);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
    TEST_ASSERT_NULL(receiver.blob.data);
    TEST_ASSERT_EQUAL_UINT(1, receiver.attempts);
    artwork_receiver_step(&receiver, NULL, ARTWORK_PER_TRACK, now + 100, nonblocking, 1);
    TEST_ASSERT_NULL(receiver.blob.data);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
    artwork_receiver_close(&receiver);
    close(listener);
}

/**
 * @brief Keep a progressing response alive, then cancel it after five idle seconds.
 */
static void progress_timeout(void)
{
    int pair[2];

    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    TEST_ASSERT_EQUAL_INT(0, nonblocking(pair[0]));

    TrackMetadata metadata = { 0 };

    strcpy(metadata.artwork_url, "http://127.0.0.1/cover");

    ArtworkReceiver receiver = { .initialized = 1, .fd = pair[0], .started = 100, .progress_at = 100, .attempts = 1 };

    receiver.blob.metadata = metadata;

    const char header[] = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na";

    TEST_ASSERT_EQUAL_INT(sizeof(header) - 1, send(pair[1], header, sizeof(header) - 1, 0));
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 4100, nonblocking, 1);
    TEST_ASSERT_EQUAL_UINT(1, receiver.http.used);
    TEST_ASSERT_EQUAL_INT(1, send(pair[1], "b", 1, 0));
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 8100, nonblocking, 1);
    TEST_ASSERT_EQUAL_UINT(2, receiver.http.used);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, receiver.fd);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 13099, nonblocking, 1);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, receiver.fd);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 13109, nonblocking, 1);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("RECEIVE BODY", receiver.stage);
    TEST_ASSERT_EQUAL_STRING("TIMEOUT", receiver.failure);
    TEST_ASSERT_EQUAL_UINT(sizeof(header), receiver.received);
#endif
    TEST_ASSERT_EQUAL_UINT(2, receiver.http.used);
    TEST_ASSERT_NULL(receiver.blob.data);
    artwork_receiver_close(&receiver);
    close(pair[1]);
}

/**
 * @brief Preserve an incomplete request's phase and byte counts after a timeout.
 */
static void send_timeout(void)
{
    int pair[2];

    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, pair));

    TrackMetadata metadata = { 0 };

    strcpy(metadata.artwork_url, "http://127.0.0.1/cover");

    ArtworkReceiver receiver = { .initialized = 1, .fd = pair[0], .started = 100, .progress_at = 100, .attempts = 1, .sent = 8, .request_size = 100 };

    receiver.blob.metadata = metadata;
#if STROOM_DIAGNOSTICS
    receiver.stage = "SEND";
#endif
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 5100, nonblocking, 1);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("SEND", receiver.stage);
    TEST_ASSERT_EQUAL_STRING("TIMEOUT", receiver.failure);
#endif
    TEST_ASSERT_EQUAL_UINT(8, receiver.sent);
    TEST_ASSERT_EQUAL_UINT(100, receiver.request_size);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0, receiver.received);
#endif
    artwork_receiver_close(&receiver);
    close(pair[1]);
}

/**
 * @brief Distinguish incomplete response headers from an image-body stall.
 */
static void header_timeout(void)
{
    int pair[2];

    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    TEST_ASSERT_EQUAL_INT(0, nonblocking(pair[0]));

    TrackMetadata metadata = { 0 };

    strcpy(metadata.artwork_url, "http://127.0.0.1/cover");

    ArtworkReceiver receiver = { .initialized = 1, .fd = pair[0], .started = 100, .progress_at = 100, .attempts = 1, .sent = 100, .request_size = 100 };

    receiver.blob.metadata = metadata;

    const char partial[] = "HTTP/1.1 200";

    TEST_ASSERT_EQUAL_INT(sizeof(partial) - 1, send(pair[1], partial, sizeof(partial) - 1, 0));
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 110, nonblocking, 1);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 5110, nonblocking, 1);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("RECEIVE HEADERS", receiver.stage);
    TEST_ASSERT_EQUAL_STRING("TIMEOUT", receiver.failure);
    TEST_ASSERT_EQUAL_UINT(sizeof(partial) - 1, receiver.received);
#endif
    TEST_ASSERT_EQUAL_UINT(0, receiver.http.used);
    TEST_ASSERT_EQUAL_UINT(receiver.request_size, receiver.sent);
    artwork_receiver_close(&receiver);
    close(pair[1]);
}

/**
 * @brief Recover after three failed attempts without requiring a metadata change.
 */
static void retry_after_failures(void)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);

    if (listener < 0)
    {
        TEST_FAIL_MESSAGE("Cannot create artwork retry fixture listener");
        return;
    }

    struct sockaddr_in address = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };

    TEST_ASSERT_EQUAL_INT(0, bind(listener, (struct sockaddr*)&address, sizeof(address)));
    TEST_ASSERT_EQUAL_INT(0, listen(listener, 1));

    socklen_t size = sizeof(address);

    TEST_ASSERT_EQUAL_INT(0, getsockname(listener, (struct sockaddr*)&address, &size));
    TEST_ASSERT_EQUAL_INT(0, nonblocking(listener));

    TrackMetadata metadata = { 0 };

    snprintf(metadata.artwork_url, sizeof(metadata.artwork_url), "http://127.0.0.1:%u/cover", ntohs(address.sin_port));

    ArtworkReceiver receiver = { .initialized = 1, .fd = -1, .attempts = 3, .retry_at = 100, .sent = 10 };

    receiver.blob.metadata = metadata;
#if STROOM_DIAGNOSTICS
    receiver.failure  = "TIMEOUT";
    receiver.received = 99;
#endif
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 30090, nonblocking, 1);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 30100, nonblocking, 1);
    TEST_ASSERT_EQUAL_UINT(4, receiver.attempts);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_NULL(receiver.failure);
    TEST_ASSERT_EQUAL_UINT(0, receiver.received);
#endif
    TEST_ASSERT_EQUAL_UINT(0, receiver.sent);

    int peer = accept(listener, NULL, NULL);

    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, peer);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 30110, nonblocking, 1);

    const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx";

    TEST_ASSERT_EQUAL_INT(sizeof(response) - 1, send(peer, response, sizeof(response) - 1, 0));
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_TRACK, 30120, nonblocking, 1);

    ArtworkBlob blob;

    TEST_ASSERT_TRUE(artwork_receiver_take(&receiver, &blob) == 0);
    TEST_ASSERT_EQUAL_UINT(1, blob.size);
    TEST_ASSERT_EQUAL_UINT8('x', blob.data[0]);
    free(blob.data);
    artwork_receiver_close(&receiver);
    close(peer);
    close(listener);
}

/** @brief Supply a queued body and verify byte/count and elapsed-time burst limits. */
static void body_burst(unsigned read_limit, unsigned clock_increment, unsigned expected)
{
    int sockets[2];

    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    TEST_ASSERT_EQUAL_INT(0, nonblocking(sockets[0]));

    TrackMetadata metadata = { 0 };

    strcpy(metadata.artwork_url, "http://127.0.0.1/cover.jpg");

    ArtworkReceiver receiver = { .initialized = 1, .fd = sockets[0], .started = 100, .progress_at = 100 };

    receiver.blob.metadata = metadata;

    const char header[] = "HTTP/1.1 200 OK\r\nContent-Length: 32768\r\n\r\n";

    TEST_ASSERT_EQUAL_INT(1, artwork_http_feed(&receiver.http, header, sizeof(header) - 1, NULL));
    memset(image, 0x5a, 32768);
    TEST_ASSERT_EQUAL_INT(32768, send(sockets[1], image, 32768, 0));

    observation_clock = UINT32_MAX - 1; /* Exercise elapsed-time wrap too. */
    clock_step        = clock_increment;

    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_URL, 110, nonblocking, read_limit);
    TEST_ASSERT_EQUAL_UINT(expected, receiver.http.used);
    TEST_ASSERT_FALSE(receiver.complete);
    /* Repeated service at the same timestamp cannot bypass polling. */
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_URL, 110, nonblocking, read_limit);
    TEST_ASSERT_EQUAL_UINT(expected, receiver.http.used);

    clock_step = 0;

    for (uint32_t now = 210; !receiver.complete && now < 1010; now += 100)
    {
        artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_URL, now, nonblocking, 4);
    }

    ArtworkBlob blob = { 0 };

    TEST_ASSERT_TRUE(artwork_receiver_take(&receiver, &blob) == 0);
    TEST_ASSERT_EQUAL_UINT(32768, blob.size);
    TEST_ASSERT_EQUAL_MEMORY(image, blob.data, blob.size);
    free(blob.data);
    artwork_receiver_close(&receiver);
    close(sockets[1]);

    clock_step = 1;
}

/** @brief CD can read four chunks; Aria and slow RPCs still yield after one. */
static void bounded_reads(void)
{
    body_burst(4, 0, 16384);
    body_burst(1, 0, 4096);
    body_burst(4, 4, 4096);
}

/** @brief A partial body, empty socket or disconnect must not spin within a burst. */
static void stalled_burst(void)
{
    int sockets[2];

    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    TEST_ASSERT_EQUAL_INT(0, nonblocking(sockets[0]));

    TrackMetadata metadata = { 0 };

    strcpy(metadata.artwork_url, "http://127.0.0.1/cover.jpg");

    ArtworkReceiver receiver = { .initialized = 1, .fd = sockets[0], .started = 100, .progress_at = 100 };

    receiver.blob.metadata = metadata;

    const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort";

    TEST_ASSERT_EQUAL_INT(sizeof(response) - 1, send(sockets[1], response, sizeof(response) - 1, 0));
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_URL, 110, nonblocking, 4);
    TEST_ASSERT_EQUAL_UINT(5, receiver.http.used);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_URL, 210, nonblocking, 4);
    TEST_ASSERT_EQUAL_UINT(5, receiver.http.used);
    TEST_ASSERT_FALSE(receiver.complete);
    close(sockets[1]);
    artwork_receiver_step(&receiver, &metadata, ARTWORK_PER_URL, 310, nonblocking, 4);
    TEST_ASSERT_EQUAL_INT(-1, receiver.fd);
    TEST_ASSERT_NULL(receiver.blob.data);
    TEST_ASSERT_FALSE(receiver.complete);
    artwork_receiver_close(&receiver);
}

/**
 * @brief Run image transport regressions.
 * @return Failed case count.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(bounded_reads);
    RUN_TEST(stalled_burst);
    RUN_TEST(urls);
    RUN_TEST(bodies);
    RUN_TEST(bad_responses);
    RUN_TEST(fetch);
    RUN_TEST(progress_timeout);
    RUN_TEST(send_timeout);
    RUN_TEST(header_timeout);
    RUN_TEST(retry_after_failures);

    return UNITY_END();
}
