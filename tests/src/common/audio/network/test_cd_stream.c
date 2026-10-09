#include "audio/network/cd_stream/server.h"
#include "contracts/audio.h"
#include "unity.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include "platform/network/tcp_state.h"
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

static CdStreamServer server;
static int            peers[16];
static unsigned       peer_count;
static uint32_t       now;
static unsigned       send_limit;
static int            injected_error, inject_after;
static in_port_t      blocked_port;
static int            collapsed_accept, collapsed_receive;
static int            reject_reuse;
static unsigned       rejected_options;
static unsigned       tcp_queries, tcp_supported;
static unsigned       driver_queries;

void platform_net_tx_stats(Ps2NetTxStats* stats)
{
    ++driver_queries;
    memset(stats, 0, sizeof(*stats));

    stats->abi          = PS2_NET_TX_STATS_ABI;
    stats->status       = 1;
    stats->link_mode    = 4;
    stats->tx_underruns = 2;
    stats->tx_errors    = 2;
    stats->tx_dropped   = 2;
}

int __real_getsockopt(int fd, int level, int option, void* value, socklen_t* size);

int __wrap_getsockopt(int fd, int level, int option, void* value, socklen_t* size)
{
    if (option == PS2_TCP_STATE_OPTION)
    {
        ++tcp_queries;

        if (!tcp_supported)
        {
            errno = ENOPROTOOPT;

            return -1;
        }

        Ps2TcpState state = { .abi = PS2_TCP_STATE_ABI, .last_ack = 12345, .send_space = 0, .unacked_bytes = 5840, .unacked_segments = 4, .rto_ms = 1500, .timer_ms = 500 };

        TEST_ASSERT_GREATER_OR_EQUAL_UINT(sizeof(state), *size);
        memcpy(value, &state, sizeof(state));

        *size = sizeof(state);

        return 0;
    }

    return __real_getsockopt(fd, level, option, value, size);
}

ssize_t __real_send(int fd, const void* data, size_t bytes, int flags);

ssize_t __wrap_send(int fd, const void* data, size_t bytes, int flags)
{
    struct sockaddr_in peer   = { 0 };
    socklen_t          length = sizeof(peer);

    if (blocked_port && getpeername(fd, (struct sockaddr*)&peer, &length) == 0 && peer.sin_port == blocked_port)
    {
        errno = EAGAIN;

        return -1;
    }

    if (inject_after >= 0 && inject_after-- == 0)
    {
        errno = injected_error;

        return injected_error ? -1 : 0;
    }

    if (bytes > send_limit)
    {
        bytes = send_limit;
    }

    return __real_send(fd, data, bytes, flags);
}

ssize_t __real_recvfrom(int fd, void* data, size_t bytes, int flags, struct sockaddr* address, socklen_t* length);

ssize_t __wrap_recvfrom(int fd, void* data, size_t bytes, int flags, struct sockaddr* address, socklen_t* length)
{
    if (collapsed_receive)
    {
        collapsed_receive = 0;
        errno             = ENFILE;

        return -1;
    }

    return __real_recvfrom(fd, data, bytes, flags, address, length);
}

int __real_accept(int fd, struct sockaddr* address, socklen_t* length);

int __wrap_accept(int fd, struct sockaddr* address, socklen_t* length)
{
    if (collapsed_accept)
    {
        collapsed_accept = 0;
        errno            = ENFILE;

        return -1;
    }

    return __real_accept(fd, address, length);
}

int __real_setsockopt(int fd, int level, int option, const void* value, socklen_t size);

int __wrap_setsockopt(int fd, int level, int option, const void* value, socklen_t size)
{
    if (reject_reuse && option == SO_REUSEADDR)
    {
        ++rejected_options;
        /* Keep Linux TIME_WAIT from leaking between tests; emulate the PS2
         * option failure at the API boundary while retaining host reuse. */
        (void)__real_setsockopt(fd, level, option, value, size);

        errno = ENOPROTOOPT;

        return -1;
    }

    return __real_setsockopt(fd, level, option, value, size);
}

static int nonblocking(int fd)
{
    int enabled = 1;

    return ioctl(fd, FIONBIO, &enabled);
}

static void tick(void)
{
    now += 10;

    TEST_ASSERT_EQUAL_INT(0, cd_stream_server_step(&server, now, "127.0.0.1"));
}

void setUp(void)
{
    now               = 0;
    peer_count        = 0;
    send_limit        = UINT_MAX;
    blocked_port      = 0;
    inject_after      = -1;
    injected_error    = 0;
    reject_reuse      = 0;
    collapsed_receive = 0;
    collapsed_accept  = 0;
    rejected_options  = 0;
    driver_queries    = 0;
    tcp_supported     = 0;
    tcp_queries       = 0;

    TEST_ASSERT_EQUAL_INT(0, cd_stream_server_open(&server, nonblocking));
}

void tearDown(void)
{
    cd_stream_server_close(&server);

    for (unsigned i = 0; i < peer_count; ++i)
    {
        close(peers[i]);
    }
}

static struct sockaddr_in destination(void)
{
    struct sockaddr_in peer = { 0 };
    peer.sin_family         = AF_INET;
    peer.sin_port           = htons(CD_STREAM_PORT);
    peer.sin_addr.s_addr    = htonl(INADDR_LOOPBACK);

    return peer;
}

static int connect_peer(int subscribe)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        TEST_FAIL_MESSAGE("Could not create test TCP socket");
        return -1;
    }

    struct sockaddr_in peer = destination();

    TEST_ASSERT_EQUAL_INT(0, connect(fd, (struct sockaddr*)&peer, sizeof(peer)));
    TEST_ASSERT_EQUAL_INT(0, nonblocking(fd));

    peers[peer_count++] = fd;

    if (subscribe)
    {
        const char request[] = CD_STREAM_REQUEST;

        TEST_ASSERT_EQUAL_INT(sizeof(request) - 1, write(fd, request, sizeof(request) - 1));
    }

    tick();

    return fd;
}

static void exact(int fd, void* output, unsigned bytes)
{
    unsigned used = 0;

    for (unsigned attempt = 0; attempt < 1000 && used < bytes; ++attempt)
    {
        tick();

        int n = recv(fd, (char*)output + used, bytes - used, 0);

        if (n > 0)
        {
            used += (unsigned)n;
        }
        else
        {
            TEST_ASSERT_TRUE(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
            usleep(1000);
        }
    }

    TEST_ASSERT_EQUAL_UINT(bytes, used);
}

static uint32_t get_word(const uint8_t* p)
{
    uint32_t value;

    memcpy(&value, p, sizeof(value));

    return ntohl(value);
}

static unsigned next_record(int fd, unsigned expected, uint8_t* payload)
{
    uint8_t header[CD_STREAM_HEADER_BYTES];

    exact(fd, header, sizeof(header));
    TEST_ASSERT_EQUAL_MEMORY(CD_STREAM_MAGIC, header, 4);
    TEST_ASSERT_EQUAL_UINT(expected, get_word(header + 4));

    unsigned bytes = get_word(header + 8);

    TEST_ASSERT_LESS_OR_EQUAL_UINT(CD_STREAM_MAX_PAYLOAD_BYTES, bytes);

    if (bytes)
    {
        exact(fd, payload, bytes);
    }

    return bytes;
}

static int subscriber(void)
{
    int     fd = connect_peer(1);
    uint8_t format[CD_STREAM_MAX_PAYLOAD_BYTES];

    TEST_ASSERT_EQUAL_UINT(12, next_record(fd, CD_STREAM_FORMAT, format));
    TEST_ASSERT_EQUAL_UINT(AUDIO_RATE, get_word(format));
    TEST_ASSERT_EQUAL_UINT(2, get_word(format + 4));
    TEST_ASSERT_EQUAL_UINT(16, get_word(format + 8));

    return fd;
}

static void expect_closed(int fd)
{
    char byte;
    int  n = -1;

    for (unsigned i = 0; i < 1000; ++i)
    {
        tick();

        n = recv(fd, &byte, 1, 0);

        if (n == 0)
        {
            break;
        }

        TEST_ASSERT_TRUE(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        usleep(1000);
    }

    TEST_ASSERT_EQUAL_INT(0, n);
}

static void discovery_requires_exact_query_and_ready_address(void)
{
    int fd              = socket(AF_INET, SOCK_DGRAM, 0);
    peers[peer_count++] = fd;

    TEST_ASSERT_EQUAL_INT(0, nonblocking(fd));

    struct sockaddr_in peer    = destination();
    const char         query[] = CD_STREAM_DISCOVER;

    TEST_ASSERT_EQUAL_INT(sizeof(query) - 1, sendto(fd, query, sizeof(query) - 1, 0, (struct sockaddr*)&peer, sizeof(peer)));
    TEST_ASSERT_EQUAL_INT(0, cd_stream_server_step(&server, now, ""));

    char reply[1024];
    server.source = (CdStreamSource){ .blocks = 75, .gaps = 1, .max_gap_ms = 60, .silence_blocks = 2 };

    TEST_ASSERT_EQUAL_INT(-1, recv(fd, reply, sizeof(reply), 0));
    TEST_ASSERT_EQUAL_INT(sizeof(query) - 1, sendto(fd, query, sizeof(query) - 1, 0, (struct sockaddr*)&peer, sizeof(peer)));
    tick();

    int n = recv(fd, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN_INT(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "\"service\":\"stroom-cd\""));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"sample_rate\":48000"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"source\":{\"blocks\":75,\"gaps\":1,\"max_gap_ms\":60"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"silence_blocks\":2"));

    const char bad[] = "DISCOVER_AUDIOCAST";

    TEST_ASSERT_GREATER_THAN_INT(0, sendto(fd, bad, sizeof(bad) - 1, 0, (struct sockaddr*)&peer, sizeof(peer)));
    tick();
    TEST_ASSERT_EQUAL_INT(-1, recv(fd, reply, sizeof(reply), 0));
}

static void independent_clients_receive_exact_pcm_and_clean_end(void)
{
    uint8_t pcm[CD_STREAM_MAX_PAYLOAD_BYTES], received[CD_STREAM_MAX_PAYLOAD_BYTES];

    for (unsigned i = 0; i < sizeof(pcm); ++i)
    {
        pcm[i] = (uint8_t)(i * 37);
    }

    /* Once the opening has left the ring, late joiners start live. */

    for (unsigned i = 0; i <= CD_STREAM_BLOCKS; ++i)
    {
        cd_stream_server_push(&server, pcm);
    }

    int first = subscriber(), second = subscriber();

    TEST_ASSERT_EQUAL_UINT(2, cd_stream_server_clients(&server));

    pcm[0] ^= 0xff;

    cd_stream_server_push(&server, pcm);
    TEST_ASSERT_EQUAL_UINT(sizeof(pcm), next_record(first, CD_STREAM_PCM, received));
    TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
    TEST_ASSERT_EQUAL_UINT(sizeof(pcm), next_record(second, CD_STREAM_PCM, received));
    TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
    cd_stream_server_end(&server);
    TEST_ASSERT_EQUAL_UINT(0, next_record(first, CD_STREAM_END, received));
    TEST_ASSERT_EQUAL_UINT(0, next_record(second, CD_STREAM_END, received));
    expect_closed(first);
    expect_closed(second);
}

static void subscriptions_during_startup_receive_the_complete_opening(void)
{
    uint8_t pcm[CD_STREAM_MAX_PAYLOAD_BYTES], received[CD_STREAM_MAX_PAYLOAD_BYTES];

    /* Discovery/reconnection finishes after the first submitted sectors. */

    for (unsigned i = 0; i < CD_STREAM_BLOCKS; ++i)
    {
        memset(pcm, (int)i, sizeof(pcm));
        cd_stream_server_push(&server, pcm);
    }

    int first = subscriber(), second = subscriber();

    for (unsigned i = 0; i < CD_STREAM_BLOCKS; ++i)
    {
        memset(pcm, (int)i, sizeof(pcm));
        TEST_ASSERT_EQUAL_UINT(sizeof(pcm), next_record(first, CD_STREAM_PCM, received));
        TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
        TEST_ASSERT_EQUAL_UINT(sizeof(pcm), next_record(second, CD_STREAM_PCM, received));
        TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
    }

    memset(pcm, 0xd3, sizeof(pcm));
    cd_stream_server_push(&server, pcm);
    cd_stream_server_end(&server);
    next_record(first, CD_STREAM_PCM, received);
    TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
    next_record(second, CD_STREAM_PCM, received);
    TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
    next_record(first, CD_STREAM_END, received);
    next_record(second, CD_STREAM_END, received);
    expect_closed(first);
    expect_closed(second);
}

static void short_writes_preserve_headers_and_samples(void)
{
    send_limit = 17;
    int     fd = subscriber();
    uint8_t pcm[CD_STREAM_MAX_PAYLOAD_BYTES], received[CD_STREAM_MAX_PAYLOAD_BYTES];

    memset(pcm, 0xa7, sizeof(pcm));
    cd_stream_server_push(&server, pcm);
    TEST_ASSERT_EQUAL_UINT(sizeof(pcm), next_record(fd, CD_STREAM_PCM, received));
    TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
}

static void temporary_send_failures_preserve_partial_record(void)
{
    const int failures[] = { ENOMEM, ENOBUFS, 0 };
    uint8_t   pcm[CD_STREAM_MAX_PAYLOAD_BYTES], received[CD_STREAM_MAX_PAYLOAD_BYTES];
    int       fd = subscriber();

    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i)
    {
        memset(pcm, (int)i + 41, sizeof(pcm));

        send_limit     = 512;
        injected_error = failures[i];
        inject_after   = 1;

        cd_stream_server_push(&server, pcm);
        TEST_ASSERT_EQUAL_UINT(sizeof(pcm), next_record(fd, CD_STREAM_PCM, received));
        TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
    }

    TEST_ASSERT_EQUAL_UINT(1, cd_stream_server_clients(&server));
}

static void brief_network_stall_preserves_queued_audio(void)
{
    int                fd    = subscriber();
    struct sockaddr_in local = { 0 };
    socklen_t          size  = sizeof(local);
    uint8_t            pcm[CD_STREAM_MAX_PAYLOAD_BYTES], received[CD_STREAM_MAX_PAYLOAD_BYTES];

    TEST_ASSERT_EQUAL_INT(0, getsockname(fd, (struct sockaddr*)&local, &size));

    blocked_port = local.sin_port;

    for (unsigned i = 0; i < 60; ++i)
    {
        memset(pcm, (int)i, sizeof(pcm));
        cd_stream_server_push(&server, pcm);
        tick();
    }

    TEST_ASSERT_EQUAL_UINT(1, cd_stream_server_clients(&server));

    blocked_port = 0;

    for (unsigned i = 0; i < 60; ++i)
    {
        memset(pcm, (int)i, sizeof(pcm));
        TEST_ASSERT_EQUAL_UINT(sizeof(pcm), next_record(fd, CD_STREAM_PCM, received));
        TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
    }
}

static void tcp_stall_snapshot_survives_recovery_and_reset(void)
{
    tcp_supported = 1;
    int fd        = subscriber();

    TEST_ASSERT_EQUAL_UINT(1, tcp_queries);

    struct sockaddr_in local = { 0 }, peer = destination();
    socklen_t          size = sizeof(local);

    TEST_ASSERT_EQUAL_INT(0, getsockname(fd, (struct sockaddr*)&local, &size));

    blocked_port                             = local.sin_port;
    uint8_t pcm[CD_STREAM_MAX_PAYLOAD_BYTES] = { 93 }, received[CD_STREAM_MAX_PAYLOAD_BYTES];

    cd_stream_server_push(&server, pcm);
    tick();

    for (unsigned i = 0; i < 24; ++i)
    {
        tick();
    }

    TEST_ASSERT_EQUAL_UINT(1, tcp_queries); /* No per-poll diagnostic RPC. */
    TEST_ASSERT_EQUAL_UINT(0, driver_queries);
    tick();
    TEST_ASSERT_EQUAL_UINT(2, tcp_queries);
    TEST_ASSERT_EQUAL_UINT(1, driver_queries);

    blocked_port = 0;

    next_record(fd, CD_STREAM_PCM, received);
    TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
    cd_stream_server_reset(&server);

    int udp             = socket(AF_INET, SOCK_DGRAM, 0);
    peers[peer_count++] = udp;

    TEST_ASSERT_EQUAL_INT(0, nonblocking(udp));

    const char query[] = CD_STREAM_TRACE;

    TEST_ASSERT_EQUAL_INT(sizeof(query) - 1, sendto(udp, query, sizeof(query) - 1, 0, (struct sockaddr*)&peer, sizeof(peer)));
    tick();

    char reply[1024];
    int  n = recv(udp, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN_INT(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "\"traceVersion\":1,\"id\":1"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"sample_ms\":250"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"available\":1"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"last_ack\":12345"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"unacked_bytes\":5840"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"rto_ms\":1500"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"driver\":{\"status\":1"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"tx_underruns\":2"));
}

static void send_stall_is_reported_in_discovery(void)
{
    int                fd    = subscriber();
    struct sockaddr_in local = { 0 }, peer = destination();
    socklen_t          size                             = sizeof(local);
    uint8_t            pcm[CD_STREAM_MAX_PAYLOAD_BYTES] = { 0 };

    TEST_ASSERT_EQUAL_INT(0, getsockname(fd, (struct sockaddr*)&local, &size));

    blocked_port = local.sin_port;

    cd_stream_server_push(&server, pcm);
    tick();

    now += 1501;

    tick();
    expect_closed(fd);

    int udp             = socket(AF_INET, SOCK_DGRAM, 0);
    peers[peer_count++] = udp;

    TEST_ASSERT_EQUAL_INT(0, nonblocking(udp));

    const char query[] = CD_STREAM_DISCOVER;

    TEST_ASSERT_EQUAL_INT(sizeof(query) - 1, sendto(udp, query, sizeof(query) - 1, 0, (struct sockaddr*)&peer, sizeof(peer)));
    tick();

    char reply[1024];
    int  n = recv(udp, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN_INT(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "\"disconnects\":1"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"last_disconnect\":\"send_stall\""));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"send_retries\":2"));
}

static void slow_client_is_dropped_without_affecting_another(void)
{
    int                slow = subscriber(), fast = subscriber();
    struct sockaddr_in local = { 0 };
    socklen_t          size  = sizeof(local);

    TEST_ASSERT_EQUAL_INT(0, getsockname(slow, (struct sockaddr*)&local, &size));

    blocked_port                             = local.sin_port;
    uint8_t pcm[CD_STREAM_MAX_PAYLOAD_BYTES] = { 0 }, received[CD_STREAM_MAX_PAYLOAD_BYTES];

    for (unsigned i = 0; i < CD_STREAM_BLOCKS + 3; ++i)
    {
        pcm[0] = (uint8_t)i;

        cd_stream_server_push(&server, pcm);
        TEST_ASSERT_EQUAL_UINT(sizeof(pcm), next_record(fast, CD_STREAM_PCM, received));
        TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
    }

    TEST_ASSERT_EQUAL_UINT(1, cd_stream_server_clients(&server));
    expect_closed(slow);
}

static void reset_cancels_old_pcm_and_allows_reconnection(void)
{
    int     fd                               = subscriber();
    uint8_t pcm[CD_STREAM_MAX_PAYLOAD_BYTES] = { 42 }, received[CD_STREAM_MAX_PAYLOAD_BYTES];

    cd_stream_server_push(&server, pcm);
    cd_stream_server_reset(&server);
    expect_closed(fd); /* No END: listener must flush instead of draining. */

    pcm[0] = 99;

    cd_stream_server_push(&server, pcm);

    fd = subscriber(); /* Reconnection must retain only the new session's opening. */

    TEST_ASSERT_EQUAL_UINT(sizeof(pcm), next_record(fd, CD_STREAM_PCM, received));
    TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
}

static void malformed_and_idle_handshakes_are_closed(void)
{
    int bad = connect_peer(0);

    TEST_ASSERT_EQUAL_INT(3, write(bad, "BAD", 3));
    tick();
    expect_closed(bad);

    int idle = connect_peer(0);
    now += 5001;

    tick();
    expect_closed(idle);
    TEST_ASSERT_EQUAL_UINT(0, cd_stream_server_clients(&server));
}

static void collapsed_ps2_would_block_keeps_discovery_available(void)
{
    collapsed_receive = 1;
    collapsed_accept  = 1;

    tick(); /* SO_ERROR is zero on the otherwise healthy sockets. */
    TEST_ASSERT_EQUAL_UINT(0, cd_stream_server_clients(&server));
    subscriber();
    TEST_ASSERT_EQUAL_UINT(1, cd_stream_server_clients(&server));
}

static void joining_after_completion_waits_for_new_pcm(void)
{
    int     first                            = subscriber();
    uint8_t pcm[CD_STREAM_MAX_PAYLOAD_BYTES] = { 37 }, received[CD_STREAM_MAX_PAYLOAD_BYTES];

    cd_stream_server_push(&server, pcm);
    cd_stream_server_end(&server);
    next_record(first, CD_STREAM_PCM, received);
    next_record(first, CD_STREAM_END, received);
    expect_closed(first);

    int next = subscriber();

    tick();

    char probe;

    TEST_ASSERT_EQUAL_INT(-1, recv(next, &probe, 1, 0));
    TEST_ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);

    pcm[0] = 99;

    cd_stream_server_push(&server, pcm);
    next_record(next, CD_STREAM_PCM, received);
    TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
}

static void unsupported_address_reuse_does_not_prevent_streaming(void)
{
    cd_stream_server_close(&server);

    reject_reuse = 1;

    TEST_ASSERT_EQUAL_INT(0, cd_stream_server_open(&server, nonblocking));
    TEST_ASSERT_EQUAL_UINT(2, rejected_options); /* Both TCP and UDP reject the option. */

    int     fd                               = subscriber();
    uint8_t pcm[CD_STREAM_MAX_PAYLOAD_BYTES] = { 91 }, received[CD_STREAM_MAX_PAYLOAD_BYTES];

    cd_stream_server_push(&server, pcm);
    TEST_ASSERT_EQUAL_UINT(sizeof(pcm), next_record(fd, CD_STREAM_PCM, received));
    TEST_ASSERT_EQUAL_MEMORY(pcm, received, sizeof(pcm));
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    UNITY_BEGIN();
    RUN_TEST(unsupported_address_reuse_does_not_prevent_streaming);
    RUN_TEST(collapsed_ps2_would_block_keeps_discovery_available);
    RUN_TEST(joining_after_completion_waits_for_new_pcm);
    RUN_TEST(discovery_requires_exact_query_and_ready_address);
    RUN_TEST(independent_clients_receive_exact_pcm_and_clean_end);
    RUN_TEST(subscriptions_during_startup_receive_the_complete_opening);
    RUN_TEST(short_writes_preserve_headers_and_samples);
    RUN_TEST(temporary_send_failures_preserve_partial_record);
    RUN_TEST(brief_network_stall_preserves_queued_audio);
    RUN_TEST(send_stall_is_reported_in_discovery);
    RUN_TEST(tcp_stall_snapshot_survives_recovery_and_reset);
    RUN_TEST(slow_client_is_dropped_without_affecting_another);
    RUN_TEST(reset_cancels_old_pcm_and_allows_reconnection);
    RUN_TEST(malformed_and_idle_handshakes_are_closed);

    return UNITY_END();
}
