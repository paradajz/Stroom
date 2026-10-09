#include "audio/network/ariacast/diagnostics/requests.h"
#include "audio/network/ariacast/server.h"
#include "audio/network/ariacast/playback.h"
#include "audio/network/ariacast/diagnostics/poll.h"
#include "unity.h"
#include "platform/network/rpc/driver_stats.h"
#include "platform/network/diagnostic/wire.h"
#include "platform/time/clock.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static Ps2DiagnosticReply diagnostic_reply;
static AriaServer         server;
static AriaPlayback       playback;
static OutputOwner        selected;
static int                initialized;
static int                prepared;
static int                stopped;
static int                stop_result;
static int                write_result;
static unsigned           captured;
static unsigned           cleared;
static unsigned           submitted_bytes;
static uint8_t            submitted[ARIA_PCM_BYTES];
static int                peers[16];
static unsigned           peer_count;
static uint32_t           now;
static unsigned           messages;
static unsigned           message_opcode;
static unsigned           message_size;
static uint8_t            decoded[ARIA_MESSAGE_BYTES];
static volatile int       running = 1;

/**
 * @brief Supply deterministic milliseconds for diagnostic timing.
 * @return Fixed test time.
 */
uint32_t platform_millis(void)
{
    return 100;
}

/**
 * @brief Return the configured optional IOP diagnostic response.
 * @param request Unused request.
 * @param reply Destination.
 */
void platform_net_diagnostic(const Ps2DiagnosticRequest* request, Ps2DiagnosticReply* reply)
{
    (void)request;

    *reply = diagnostic_reply;
}

int output_write_timed(OutputOwner owner, const uint8_t* pcm, unsigned bytes, OutputWriteTiming* timing)
{
    memset(timing, 0, sizeof(*timing));

    timing->begin = timing->end = 100;

    return output_write(owner, pcm, bytes);
}

void platform_net_driver_stats(Ps2NetDriverStats* stats)
{
    memset(stats, 0, sizeof(*stats));

    stats->status        = 1;
    stats->rx_alloc_fail = 123;
}

int output_selected(OutputOwner owner)
{
    return selected == owner;
}

int output_initialize(const OutputRuntime* runtime)
{
    (void)runtime;
    ++initialized;

    return 0;
}

int output_prepare(OutputOwner owner, const OutputRuntime* runtime)
{
    (void)runtime;
    TEST_ASSERT_EQUAL_INT(OUTPUT_NETWORK, owner);
    ++prepared;

    return 0;
}

int output_stop(OutputOwner owner)
{
    TEST_ASSERT_EQUAL_INT(OUTPUT_NETWORK, owner);
    ++stopped;

    return stop_result ? 0 : -1;
}

int output_write(OutputOwner owner, const uint8_t* pcm, unsigned bytes)
{
    TEST_ASSERT_EQUAL_INT(OUTPUT_NETWORK, owner);

    if (write_result > 0)
    {
        submitted_bytes = bytes;

        memcpy(submitted, pcm, bytes);
    }

    return write_result < 0 ? write_result : write_result ? 0
                                                          : 1;
}

/**
 * @brief Observe analysis delivery after sound submission.
 * @param context Unused.
 * @param pcm Submitted samples.
 * @param frames Stereo sample count.
 */
static void capture(void* context, const uint8_t* pcm, unsigned frames)
{
    (void)context;

    if (frames)
    {
        TEST_ASSERT_EQUAL_UINT(ARIA_PCM_FRAMES, frames);
        TEST_ASSERT_EQUAL_MEMORY(submitted, pcm, ARIA_PCM_BYTES);
        ++captured;
    }
    else
    {
        ++cleared;
    }
}

/**
 * @brief Configure a real host socket.
 * @param fd Descriptor.
 * @return Zero on success, negative on failure.
 */
static int nonblocking(int fd)
{
    int yes = 1;

    return ioctl(fd, FIONBIO, &yes);
}

/**
 * @brief Record a parser callback.
 * @param context Unused.
 * @param opcode Frame type.
 * @param data Message bytes.
 * @param size Byte count.
 * @return One.
 */
static int decoded_message(void* context, unsigned opcode, const uint8_t* data, unsigned size)
{
    (void)context;
    ++messages;

    message_opcode = opcode;
    message_size   = size;

    memcpy(decoded, data, size);

    return 1;
}

/**
 * @brief Encode a masked client frame.
 * @param out Destination.
 * @param opcode Type.
 * @param final FIN flag.
 * @param data Payload.
 * @param size Byte count.
 * @return Wire size.
 */
static unsigned masked(uint8_t* out, unsigned opcode, int final, const uint8_t* data, unsigned size)
{
    unsigned prefix = size < 126 ? 2 : 4;

    out[0] = (uint8_t)(opcode | (final ? 128 : 0));
    out[1] = (uint8_t)(128 | (size < 126 ? size : 126));

    if (prefix == 4)
    {
        out[2] = (uint8_t)(size >> 8);
        out[3] = (uint8_t)size;
    }

    const uint8_t mask[] = { 0x11, 0x23, 0x45, 0x67 };

    memcpy(out + prefix, mask, 4);

    for (unsigned i = 0; i < size; ++i)
    {
        out[prefix + 4 + i] = data[i] ^ mask[i % 4];
    }

    return prefix + 4 + size;
}

/**
 * @brief Exercise RFC accept and incremental framing including interleaved control.
 */
static void websocket(void)
{
    char accept[29];

    TEST_ASSERT_TRUE(aria_websocket_accept("dGhlIHNhbXBsZSBub25jZQ==", accept) == 0);
    TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", accept);
    TEST_ASSERT_TRUE(!(aria_websocket_accept("not a nonce", accept) == 0));

    static AriaWebSocket ws;
    static uint8_t       source[ARIA_PCM_BYTES], wire[ARIA_MESSAGE_BYTES + 8];

    memset(&ws, 0, sizeof(ws));

    for (unsigned i = 0; i < sizeof(source); ++i)
    {
        source[i] = (uint8_t)i;
    }

    unsigned size = masked(wire, 2, 0, source, 1000);

    for (unsigned i = 0; i < size; ++i)
    {
        TEST_ASSERT_TRUE(aria_websocket_feed(&ws, wire + i, 1, decoded_message, NULL) == 0);
    }

    TEST_ASSERT_EQUAL_UINT(0, messages);

    size = masked(wire, 9, 1, source, 2);

    TEST_ASSERT_TRUE(aria_websocket_feed(&ws, wire, size, decoded_message, NULL) == 0);
    TEST_ASSERT_EQUAL_UINT(9, message_opcode);

    size = masked(wire, 0, 1, source + 1000, sizeof(source) - 1000);

    TEST_ASSERT_TRUE(aria_websocket_feed(&ws, wire, size, decoded_message, NULL) == 0);
    TEST_ASSERT_EQUAL_UINT(2, messages);
    TEST_ASSERT_EQUAL_UINT(2, message_opcode);
    TEST_ASSERT_EQUAL_UINT(sizeof(source), message_size);
    TEST_ASSERT_EQUAL_MEMORY(source, decoded, sizeof(source));
    memset(&ws, 0, sizeof(ws));

    const uint8_t unmasked[] = { 0x82, 0, 0, 0, 0, 0 };

    TEST_ASSERT_TRUE(!(aria_websocket_feed(&ws, unmasked, sizeof(unmasked), decoded_message, NULL) == 0));
    memset(&ws, 0, sizeof(ws));

    const uint8_t oversized[] = { 0x82, 0xfe, 0x20, 0, 0, 0, 0, 0 };

    TEST_ASSERT_TRUE(!(aria_websocket_feed(&ws, oversized, sizeof(oversized), decoded_message, NULL) == 0));
    memset(&ws, 0, sizeof(ws));

    size = masked(wire, 0, 1, source, 2);

    TEST_ASSERT_TRUE(!(aria_websocket_feed(&ws, wire, size, decoded_message, NULL) == 0));
}

/**
 * @brief Service sockets for an explicit duration without hiding deadline crossings.
 * @param milliseconds Simulated duration.
 */
static void pump_for(unsigned milliseconds)
{
    for (unsigned i = 0; i < milliseconds; ++i)
    {
        aria_server_step(&server, now++, "127.0.0.1", selected != OUTPUT_CD);
    }
}

/** @brief Service enough time for discovery, acceptance and stream I/O. */
static void pump(void)
{
    pump_for(120);
}

/**
 * @brief Connect a host client.
 * @return Descriptor.
 */
static int connect_client(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        TEST_FAIL_MESSAGE("Cannot create client socket");
        return -1;
    }

    struct sockaddr_in address = { 0 };

    address.sin_family      = AF_INET;
    address.sin_port        = htons(ARIA_STREAM_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    TEST_ASSERT_EQUAL_INT(0, connect(fd, (struct sockaddr*)&address, sizeof(address)));

    int yes = 1;

    TEST_ASSERT_EQUAL_INT(0, setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)));
    TEST_ASSERT_EQUAL_INT(0, nonblocking(fd));

    peers[peer_count++] = fd;

    pump();

    return fd;
}

/**
 * @brief Send an HTTP upgrade request and collect its response.
 * @param path Endpoint.
 * @param reply Destination of 2048 bytes.
 * @return Client descriptor.
 */
static int upgrade(const char* path, char* reply)
{
    int  fd = connect_client();
    char request[512];
    int  size = snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n", path);

    TEST_ASSERT_EQUAL_INT(size, send(fd, request, size, 0));
    pump();

    int n = recv(fd, reply, 2047, 0);

    TEST_ASSERT_GREATER_THAN(0, n);

    reply[n] = 0;

    return fd;
}

/**
 * @brief Send one PCM message.
 * @param fd Audio sender.
 * @param value Fill byte.
 */
static void send_pcm(int fd, uint8_t value)
{
    uint8_t data[ARIA_PCM_BYTES], wire[ARIA_PCM_BYTES + 8];

    memset(data, value, sizeof(data));

    unsigned n = masked(wire, 2, 1, data, sizeof(data));

    TEST_ASSERT_EQUAL_INT(n, send(fd, wire, n, 0));
    pump_for(ARIA_PCM_FRAMES * 1000 / AUDIO_RATE);
}

/**
 * @brief Verify discovery, single-sender ownership, handshake and PCM delivery.
 */
static void receiver(void)
{
    int udp = socket(AF_INET, SOCK_DGRAM, 0);

    TEST_ASSERT_EQUAL_INT(0, nonblocking(udp));

    struct sockaddr_in address = { 0 };

    address.sin_family      = AF_INET;
    address.sin_port        = htons(ARIA_DISCOVERY_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const char query[] = "DISCOVER_AUDIOCAST";

    TEST_ASSERT_EQUAL_INT(3, sendto(udp, "bad", 3, 0, (struct sockaddr*)&address, sizeof(address)));
    pump();

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.discovery_received);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.discovery_matched);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.discovery_replied);
#endif
    TEST_ASSERT_EQUAL_INT(sizeof(query) - 1, sendto(udp, query, sizeof(query) - 1, 0, (struct sockaddr*)&address, sizeof(address)));
    pump();

    char reply[2048];
    int  n = recv(udp, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "\"port\":12889"));

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(2, server.diagnostics.discovery_received);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.discovery_matched);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.discovery_replied);
#endif
    close(udp);

    int fd = upgrade("/audio", reply);

    TEST_ASSERT_NOT_NULL(strstr(reply, "101 Switching Protocols"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    TEST_ASSERT_NOT_NULL(strstr(reply, "READY"));
    upgrade("/audio", reply);
    TEST_ASSERT_NOT_NULL(strstr(reply, "403 Forbidden"));
    send_pcm(fd, 0x31);
    TEST_ASSERT_TRUE(aria_server_active(&server, now));
    TEST_ASSERT_EQUAL_UINT(1, server.stream.count);
    TEST_ASSERT_EQUAL_HEX8(0x31, server.stream.pcm[server.stream.read][0]);

    now += ARIA_IDLE_MS;

    pump();

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("PCM receive timeout", server.diagnostics.last_disconnect);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(ARIA_IDLE_MS, server.diagnostics.last_disconnect_gap);
#endif
    TEST_ASSERT_TRUE(aria_server_active(&server, now));
    TEST_ASSERT_EQUAL_UINT(1, server.stream.count);
    upgrade("/audio", reply);

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("PCM receive timeout", server.diagnostics.last_disconnect);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.disconnects);
#endif
}

/**
 * @brief Verify selected streaming reaches sound and analysis with bounded buffering.
 */
static void sound_delivery(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    for (unsigned i = 0; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        send_pcm(fd, (uint8_t)(i + 1));
    }

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_INT(0, prepared);

    selected     = OUTPUT_NETWORK;
    write_result = 0;
#if STROOM_DIAGNOSTICS
    AriaDiagnosticCapture* recorder    = &server.diagnostics.diagnostic_capture;
    unsigned               records     = recorder->count;
    unsigned               next_record = recorder->write;
#endif

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_INT(1, prepared);
    TEST_ASSERT_EQUAL_UINT(0, captured);
    TEST_ASSERT_EQUAL_UINT(ARIA_PREBUFFER_MESSAGES, server.stream.count);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(records + 2, recorder->count);
    TEST_ASSERT_EQUAL_UINT(DIAGNOSTIC_CAPTURE_OUTPUT, recorder->rolling[next_record].kind);
    TEST_ASSERT_EQUAL_UINT(1, recorder->rolling[next_record].data[DIAGNOSTIC_CAPTURE_OUTPUT_RESULT]);

    records = recorder->count;
#endif

    write_result = 1;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(1, captured);
    TEST_ASSERT_EQUAL_UINT(ARIA_PCM_BYTES, submitted_bytes);
    TEST_ASSERT_EQUAL_HEX8(1, submitted[0]);
    TEST_ASSERT_EQUAL_UINT(ARIA_PREBUFFER_MESSAGES - 1, server.stream.count);
#if STROOM_DIAGNOSTICS
    /* A fast accepted write must not replace shortage history with normal traffic. */
    TEST_ASSERT_EQUAL_UINT(records, recorder->count);
#endif

    selected = OUTPUT_CD;

    pump();
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_INT(1, stopped);
    TEST_ASSERT_EQUAL_UINT(1, cleared);
    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
    upgrade("/audio", reply);
    TEST_ASSERT_NOT_NULL(strstr(reply, "403 Forbidden"));
}

static void capture_listening(void* context, const uint8_t* pcm, unsigned frames)
{
    (void)context;

    if (!frames)
    {
        ++cleared;
        return;
    }

    TEST_ASSERT_EQUAL_UINT(ARIA_PCM_FRAMES, frames);
    TEST_ASSERT_EQUAL_UINT(captured + 1, pcm[0]);
    ++captured;
}

/** Verify the negotiated session reaches analysis on its clock without any sound calls. */
static void listening_delivery(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    TEST_ASSERT_NOT_NULL(strstr(reply, "\"stroom_listen\":true"));

    uint8_t  wire[64];
    unsigned size = masked(wire, ARIA_WS_TEXT, 1, (const uint8_t*)ARIA_LISTEN_REQUEST, sizeof(ARIA_LISTEN_REQUEST) - 1);

    TEST_ASSERT_EQUAL_INT(size, send(fd, wire, size, 0));
    pump();

    int received = recv(fd, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN_INT(0, received);

    reply[received] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, ARIA_LISTEN_ACK));
    TEST_ASSERT_TRUE(server.stream.listening);
    send_pcm(fd, 1);

    now                    = UINT32_MAX - 10;
    server.stream.last_pcm = now;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture_listening, NULL);
    TEST_ASSERT_EQUAL_UINT(1, captured);
    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture_listening, NULL);
    TEST_ASSERT_EQUAL_UINT(1, captured);

    now += ARIA_PCM_MESSAGE_MS - 1;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture_listening, NULL);
    TEST_ASSERT_EQUAL_UINT(1, captured);
    TEST_ASSERT_EQUAL_UINT(0, cleared);

    uint8_t pcm[ARIA_PCM_BYTES];

    memset(pcm, 2, sizeof(pcm));
    TEST_ASSERT_TRUE(aria_stream_push(&server.stream, pcm, sizeof(pcm), now) == 0);
    ++now;
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture_listening, NULL);
    TEST_ASSERT_EQUAL_UINT(2, captured);
    TEST_ASSERT_EQUAL_INT(0, initialized);
    TEST_ASSERT_EQUAL_INT(0, prepared);
    TEST_ASSERT_EQUAL_UINT(0, submitted_bytes);

    now += ARIA_IDLE_MS;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture_listening, NULL);
    TEST_ASSERT_EQUAL_UINT(1, cleared);
    TEST_ASSERT_TRUE(aria_playback_stop(&playback) == 0);
    TEST_ASSERT_EQUAL_INT(0, stopped);
    aria_server_disconnect(&server, "test reconnect");
    upgrade("/audio", reply);
    TEST_ASSERT_FALSE(server.stream.listening);
}

/** Names belong to one audio session and are bounded before acknowledgement. */
static void listening_device_names(void)
{
    const char* names[] = { "Focusrite 18i20", "Mac \"Studio\"", "bad\nname", "" };

    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
    {
        char reply[2048], request[128];
        int  fd = upgrade("/audio", reply);

        TEST_ASSERT_NOT_NULL(strstr(reply, "\"stroom_device_name_bytes\":63"));

        int      length = snprintf(request, sizeof(request), "%s %s", ARIA_LISTEN_REQUEST, names[i]);
        uint8_t  wire[256];
        unsigned size = masked(wire, ARIA_WS_TEXT, 1, (const uint8_t*)request, (unsigned)length);

        TEST_ASSERT_EQUAL_INT(size, send(fd, wire, size, 0));
        pump();

        if (i < 2)
        {
            TEST_ASSERT_TRUE(server.stream.listening);
            TEST_ASSERT_EQUAL_STRING(names[i], server.stream.device_name);

            int count = recv(fd, reply, sizeof(reply) - 1, 0);

            TEST_ASSERT_GREATER_THAN_INT(0, count);

            reply[count] = 0;

            TEST_ASSERT_NOT_NULL(strstr(reply, ARIA_LISTEN_ACK));
            aria_server_disconnect(&server, "test next name");
        }
        else
        {
            TEST_ASSERT_FALSE(server.stream.connected);
        }

        TEST_ASSERT_EQUAL_STRING("", server.stream.device_name);
    }

    for (unsigned size = METADATA_DEVICE_NAME_BYTES - 1; size <= METADATA_DEVICE_NAME_BYTES; ++size)
    {
        char reply[2048];
        int  fd = upgrade("/audio", reply);
        char request[sizeof(ARIA_LISTEN_REQUEST) + METADATA_DEVICE_NAME_BYTES];

        memcpy(request, ARIA_LISTEN_REQUEST " ", sizeof(ARIA_LISTEN_REQUEST));
        memset(request + sizeof(ARIA_LISTEN_REQUEST), 'X', size);

        uint8_t  wire[256];
        unsigned bytes = masked(wire, ARIA_WS_TEXT, 1, (const uint8_t*)request, sizeof(ARIA_LISTEN_REQUEST) + size);

        TEST_ASSERT_EQUAL_INT(bytes, send(fd, wire, bytes, 0));
        pump();
        TEST_ASSERT_EQUAL_INT(size < METADATA_DEVICE_NAME_BYTES, server.stream.connected);

        if (server.stream.connected)
        {
            aria_server_disconnect(&server, "test next size");
        }
    }

    char reply[2048];

    upgrade("/audio", reply);
    TEST_ASSERT_FALSE(server.stream.listening);
    TEST_ASSERT_EQUAL_STRING("", server.stream.device_name);
}

/** Never confirm silent mode after PCM has already been admitted. */
static void listening_requires_negotiation_before_pcm(void)
{
    char reply[2048];
    int  fd = upgrade("/audio", reply);

    send_pcm(fd, 1);

    uint8_t  wire[64];
    unsigned size = masked(wire, ARIA_WS_TEXT, 1, (const uint8_t*)ARIA_LISTEN_REQUEST, sizeof(ARIA_LISTEN_REQUEST) - 1);

    TEST_ASSERT_EQUAL_INT(size, send(fd, wire, size, 0));
    pump();
    TEST_ASSERT_FALSE(server.stream.connected);
    TEST_ASSERT_FALSE(server.stream.listening);
}

/**
 * @brief Start only after 260 ms of PCM and preserve message order through playback.
 */
static void startup_reserve(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    selected = OUTPUT_NETWORK;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    TEST_ASSERT_EQUAL_UINT(13, ARIA_PREBUFFER_MESSAGES);

    for (unsigned i = 0; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, prepared);
        TEST_ASSERT_EQUAL_UINT(0, captured);
        send_pcm(fd, (uint8_t)(i + 1));
        aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    }

    TEST_ASSERT_EQUAL_INT(1, prepared);
    TEST_ASSERT_EQUAL_UINT(1, captured);
    TEST_ASSERT_EQUAL_HEX8(1, submitted[0]);

    for (unsigned i = 1; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
        TEST_ASSERT_EQUAL_HEX8(i + 1, submitted[0]);
    }

    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
    TEST_ASSERT_EQUAL_UINT(ARIA_PREBUFFER_MESSAGES, captured);
}

/**
 * @brief Verify output errors discard the session rather than advancing its queue.
 */
static void output_error(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    for (unsigned i = 0; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        send_pcm(fd, 7);
    }

    selected     = OUTPUT_NETWORK;
    write_result = -1;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("sound output failed", server.diagnostics.last_disconnect);
#endif
    TEST_ASSERT_EQUAL_STRING("STREAM SOUND OUTPUT ERROR", error);
    TEST_ASSERT_EQUAL_UINT(0, captured);
    TEST_ASSERT_EQUAL_UINT(1, cleared);
    TEST_ASSERT_FALSE(aria_server_active(&server, now));
    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
}

/**
 * @brief Verify optional channels, split headers, metadata limits, and PCM overflow.
 */
static void protocol_limits(void)
{
    char reply[2048];
    int  control = upgrade("/control", reply);

    TEST_ASSERT_NOT_NULL(strstr(reply, "volume_available"));

    uint8_t  ping[16];
    unsigned n = masked(ping, 9, 1, (const uint8_t*)"hi", 2);

    TEST_ASSERT_EQUAL_INT(n, send(control, ping, n, 0));
    pump();
    TEST_ASSERT_EQUAL_INT(4, recv(control, reply, sizeof(reply), 0));
    TEST_ASSERT_EQUAL_HEX8(0x8a, (uint8_t)reply[0]);

    int stats = upgrade("/stats", reply);

    now += 1000;

    pump();

    int bytes = recv(stats, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, bytes);

    reply[bytes] = 0;

    TEST_ASSERT_GREATER_THAN(4, bytes);
    TEST_ASSERT_EQUAL_HEX8(0x81, (uint8_t)reply[0]);

    unsigned header_bytes = (uint8_t)reply[1] == 126 ? 4 : 2;

    TEST_ASSERT_NOT_NULL(strstr(reply + header_bytes, "receivedFrames"));
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_NOT_NULL(strstr(reply + header_bytes, "discoveryReceived"));
#else
    TEST_ASSERT_NULL(strstr(reply + header_bytes, "discoveryReceived"));
#endif
    int         fd    = connect_client();
    const char* first = "POST /metadata HTTP/1.1\r\nContent-Length: 2\r\n";

    TEST_ASSERT_EQUAL_INT(strlen(first), send(fd, first, strlen(first), 0));
    pump();
    TEST_ASSERT_EQUAL_INT(4, send(fd, "\r\n{}", 4, 0));
    pump();

    bytes = recv(fd, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, bytes);

    reply[bytes] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "200 OK"));

    fd = upgrade("/audio", reply);

    unsigned generation = server.stream.generation;

    for (unsigned i = 0; i < ARIA_QUEUE_MESSAGES * 3; ++i)
    {
        send_pcm(fd, (uint8_t)i);
    }

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.disconnects);
#endif
    TEST_ASSERT_EQUAL_UINT(generation, server.stream.generation);
    TEST_ASSERT_EQUAL_UINT(ARIA_QUEUE_MESSAGES, server.stream.received);
    TEST_ASSERT_TRUE(aria_server_active(&server, now));
    TEST_ASSERT_EQUAL_UINT(ARIA_QUEUE_MESSAGES, server.stream.count);

    for (unsigned i = 0; i < ARIA_QUEUE_MESSAGES * 3; ++i)
    {
        pump_for(ARIA_PCM_FRAMES * 1000 / AUDIO_RATE);
        TEST_ASSERT_GREATER_THAN_UINT(0, server.stream.count);
        TEST_ASSERT_EQUAL_HEX8(i, server.stream.pcm[server.stream.read][0]);
        TEST_ASSERT_EQUAL_HEX8(i, server.stream.pcm[server.stream.read][ARIA_PCM_BYTES - 1]);
        aria_stream_consume(&server.stream);
    }

    send_pcm(fd, 99);
    TEST_ASSERT_EQUAL_UINT(1, server.stream.count);
    TEST_ASSERT_EQUAL_HEX8(99, server.stream.pcm[server.stream.read][0]);
    TEST_ASSERT_EQUAL_UINT(generation, server.stream.generation);
}

/**
 * @brief Retry a failed stop without restarting or advancing the expired stream.
 */
static void stop_retry(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    for (unsigned i = 0; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        send_pcm(fd, 2);
    }

    selected = OUTPUT_NETWORK;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(1, captured);

    now += ARIA_IDLE_MS;

    pump();

    now += ARIA_IDLE_MS;

    pump();

    stop_result = 0;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_STRING("STREAM SOUND STOP ERROR", error);
    TEST_ASSERT_TRUE(playback.started);
    TEST_ASSERT_EQUAL_INT(1, stopped);
    TEST_ASSERT_EQUAL_UINT(1, captured);

    stop_result = 1;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_FALSE(playback.started);
    TEST_ASSERT_EQUAL_INT(2, stopped);
    TEST_ASSERT_EQUAL_INT(1, prepared);

    fd = upgrade("/audio", reply);

    for (unsigned i = 0; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        send_pcm(fd, 3);
    }

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_INT(2, prepared);
    TEST_ASSERT_EQUAL_HEX8(3, submitted[0]);
}

/**
 * @brief Reject ambiguous HTTP lengths and malformed PCM, and expire incomplete requests.
 */
static void malformed_requests(void)
{
    int        fd        = connect_client();
    const char request[] = "POST /metadata HTTP/1.1\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}";

    TEST_ASSERT_EQUAL_INT(sizeof(request) - 1, send(fd, request, sizeof(request) - 1, 0));
    pump();

    char reply[2048];
    int  n = recv(fd, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "400 Bad Request"));

    fd = connect_client();

    TEST_ASSERT_EQUAL_INT(3, send(fd, "GET", 3, 0));
    pump();

    now += 3000;

    pump();
    TEST_ASSERT_EQUAL_INT(0, recv(fd, reply, sizeof(reply), 0));

    fd = upgrade("/audio", reply);

    uint8_t  wire[16];
    unsigned bytes = masked(wire, 2, 1, (const uint8_t*)"bad", 3);

    TEST_ASSERT_EQUAL_INT(bytes, send(fd, wire, bytes, 0));
    pump();
    TEST_ASSERT_FALSE(aria_server_active(&server, now));
    TEST_ASSERT_EQUAL_INT(-1, server.audio_fd);
    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
}

/**
 * @brief A failed listener releases connections and buffered PCM.
 */
static void listener_failure(void)
{
    char reply[2048];
    int  fd = upgrade("/audio", reply);

    send_pcm(fd, 1);
    TEST_ASSERT_TRUE(aria_server_active(&server, now));
    close(server.listener);
    pump();

    char expected[96];

    snprintf(expected, sizeof(expected), "ARIA TCP %d ACCEPT ERR %d", ARIA_STREAM_PORT, EBADF);
    TEST_ASSERT_EQUAL_STRING(expected, server.error);
    TEST_ASSERT_EQUAL_INT(-1, server.listener);
    TEST_ASSERT_EQUAL_INT(-1, server.audio_fd);
    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
}

/**
 * @brief Model an SDK nonblocking setup failure.
 * @param fd Unused descriptor.
 * @return Zero, with errno set.
 */
static int rejected_nonblocking(int fd)
{
    (void)fd;

    errno = ENOSYS;

    return -1;
}

/**
 * @brief Retain precise startup errors through cleanup and permit a clean retry.
 */
static void startup_failures(void)
{
    aria_server_close(&server);
    TEST_ASSERT_TRUE(!(aria_server_open(&server, rejected_nonblocking) == 0));

    char expected[96];

    snprintf(expected, sizeof(expected), "ARIA TCP %d NONBLOCK ERR %d", ARIA_STREAM_PORT, ENOSYS);
    TEST_ASSERT_EQUAL_STRING(expected, server.error);
    TEST_ASSERT_EQUAL_INT(-1, server.listener);
    TEST_ASSERT_EQUAL_INT(-1, server.discovery);

    int blocker = socket(AF_INET, SOCK_DGRAM, 0);

    if (blocker < 0)
    {
        TEST_FAIL_MESSAGE("Cannot create occupied-port fixture");
        return;
    }

    peers[peer_count++] = blocker;

    struct sockaddr_in address = { 0 };

    address.sin_family      = AF_INET;
    address.sin_port        = htons(ARIA_DISCOVERY_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    TEST_ASSERT_EQUAL_INT(0, bind(blocker, (struct sockaddr*)&address, sizeof(address)));
    TEST_ASSERT_TRUE(!(aria_server_open(&server, nonblocking) == 0));
    snprintf(expected, sizeof(expected), "ARIA UDP %d BIND ERR %d", ARIA_DISCOVERY_PORT, EADDRINUSE);
    TEST_ASSERT_EQUAL_STRING(expected, server.error);
    TEST_ASSERT_EQUAL_INT(-1, server.listener);
    TEST_ASSERT_EQUAL_INT(-1, server.discovery);
    close(blocker);
    --peer_count;
    TEST_ASSERT_TRUE(aria_server_open(&server, nonblocking) == 0);
    TEST_ASSERT_EQUAL_STRING("", server.error);
}

/**
 * @brief Open an isolated real-socket receiver and reset observers.
 */
void setUp(void)
{
    signal(SIGPIPE, SIG_IGN);
    memset(&diagnostic_reply, 0, sizeof(diagnostic_reply));

    now         = 100;
    selected    = OUTPUT_NONE;
    initialized = prepared = stopped = 0;
    write_result = stop_result = 1;
    captured = cleared = submitted_bytes = peer_count = messages = 0;

    memset(&playback, 0, sizeof(playback));
    TEST_ASSERT_TRUE(aria_server_open(&server, nonblocking) == 0);
}

/**
 * @brief Close all receiver and sender sockets.
 */
void tearDown(void)
{
    aria_server_close(&server);

    for (unsigned i = 0; i < peer_count; ++i)
    {
        close(peers[i]);
    }
}

/**
 * @brief A split Close terminates decoding, including an unfinished message.
 */
static void websocket_close_terminates(void)
{
    const uint8_t status[] = { 0x03, 0xe8 };
    const uint8_t sample[] = { 1, 2 };
    uint8_t       close_wire[16], continuation[16], first[16];
    unsigned      close_size        = masked(close_wire, ARIA_WS_CLOSE, 1, status, sizeof(status));
    unsigned      first_size        = masked(first, ARIA_WS_BINARY, 0, sample, sizeof(sample));
    unsigned      continuation_size = masked(continuation, 0, 1, sample, sizeof(sample));

    for (unsigned split = 0; split <= close_size; ++split)
    {
        AriaWebSocket ws = { 0 };

        messages = 0;

        TEST_ASSERT_TRUE(aria_websocket_feed(&ws, first, first_size, decoded_message, NULL) == 0);
        TEST_ASSERT_TRUE(aria_websocket_feed(&ws, close_wire, split, decoded_message, NULL) == 0);
        TEST_ASSERT_TRUE(aria_websocket_feed(&ws, close_wire + split, close_size - split, decoded_message, NULL) == 0);
        TEST_ASSERT_EQUAL_UINT(1, messages);
        TEST_ASSERT_EQUAL_UINT(ARIA_WS_CLOSE, message_opcode);
        TEST_ASSERT_EQUAL_MEMORY(status, decoded, sizeof(status));
        TEST_ASSERT_TRUE(aria_websocket_feed(&ws, continuation, continuation_size, decoded_message, NULL) == 0);
        TEST_ASSERT_TRUE(aria_websocket_feed(&ws, close_wire, close_size, decoded_message, NULL) == 0);
        TEST_ASSERT_EQUAL_UINT(1, messages);
    }
}

/**
 * @brief Close followed by PCM in one write queues no audio and still replies.
 */
static void close_before_pcm(void)
{
    char          reply[2048];
    int           fd                  = upgrade("/audio", reply);
    const uint8_t status[]            = { 0x03, 0xe8 };
    uint8_t       pcm[ARIA_PCM_BYTES] = { 0 };
    uint8_t       wire[ARIA_PCM_BYTES + 32];
    unsigned      bytes = masked(wire, ARIA_WS_CLOSE, 1, status, sizeof(status));

    bytes += masked(wire + bytes, ARIA_WS_BINARY, 1, pcm, sizeof(pcm));

    TEST_ASSERT_EQUAL_INT(bytes, send(fd, wire, bytes, 0));
    pump();
    TEST_ASSERT_EQUAL_UINT(0, server.stream.received);
    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
    TEST_ASSERT_FALSE(aria_server_active(&server, now));

    const uint8_t expected[] = { 0x88, 2, 0x03, 0xe8 };

    TEST_ASSERT_EQUAL_INT(sizeof(expected), recv(fd, reply, sizeof(reply), 0));
    TEST_ASSERT_EQUAL_MEMORY(expected, reply, sizeof(expected));

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("peer sent WebSocket Close", server.diagnostics.last_disconnect);
#endif
    TEST_ASSERT_EQUAL_INT(0, recv(fd, reply, sizeof(reply), 0));
}

/**
 * @brief A due statistics update must not follow a queued Close reply.
 */
static void statistics_after_close(void)
{
    char          reply[2048];
    int           fd       = upgrade("/stats", reply);
    const uint8_t status[] = { 0x03, 0xe8 };
    uint8_t       wire[16];
    unsigned      bytes = masked(wire, ARIA_WS_CLOSE, 1, status, sizeof(status));

    TEST_ASSERT_EQUAL_INT(bytes, send(fd, wire, bytes, 0));
    /* Receive Close without flushing its reply, then make statistics due. */
    aria_clients_service(&server, now);

    now += 1000;

    aria_clients_tick(&server, now);
    aria_clients_service(&server, now);

    const uint8_t expected[] = { 0x88, 2, 0x03, 0xe8 };

    TEST_ASSERT_EQUAL_INT(sizeof(expected), recv(fd, reply, sizeof(reply), 0));
    TEST_ASSERT_EQUAL_MEMORY(expected, reply, sizeof(expected));
    TEST_ASSERT_EQUAL_INT(0, recv(fd, reply, sizeof(reply), 0));
}

/**
 * @brief Matching Pongs retain idle clients; incorrect or absent replies expire them.
 */
static void heartbeat(void)
{
    char reply[2048];
    int  fd = upgrade("/control", reply);

    now += 10000;

    pump();
    TEST_ASSERT_EQUAL_INT(6, recv(fd, reply, sizeof(reply), 0));
    TEST_ASSERT_EQUAL_HEX8(0x89, (uint8_t)reply[0]);

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.pings);
#endif
    uint8_t  wire[16];
    unsigned size = masked(wire, ARIA_WS_PONG, 1, (uint8_t*)reply + 2, 4);

    TEST_ASSERT_EQUAL_INT(size, send(fd, wire, size, 0));
    pump();

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.pongs);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0x7f000001, ntohl(server.diagnostics.last_pong_address));
#endif
    now += 6000;

    pump();

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.heartbeat_timeouts);
#endif
    TEST_ASSERT_EQUAL_INT(-1, recv(fd, reply, sizeof(reply), 0));

    now += 4000;

    pump();
    TEST_ASSERT_EQUAL_INT(6, recv(fd, reply, sizeof(reply), 0));
    // Replaying the preceding Pong does not acknowledge the new Ping.
    TEST_ASSERT_EQUAL_INT(size, send(fd, wire, size, 0));
    pump();

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.pongs);
#endif
    now += 5000;

    pump();

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.heartbeat_timeouts);
#endif
    TEST_ASSERT_EQUAL_INT(0, recv(fd, reply, sizeof(reply), 0));
}

/**
 * @brief Metadata heartbeat expiry remains correct across clock wraparound.
 */
static void heartbeat_wraparound(void)
{
    now = UINT32_MAX - 5000;

    char reply[2048];
    int  fd = upgrade("/metadata", reply);

    now += 10000;

    pump();
    TEST_ASSERT_EQUAL_INT(6, recv(fd, reply, sizeof(reply), 0));

    now += 5000;

    pump();

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.heartbeat_timeouts);
#endif
    TEST_ASSERT_EQUAL_INT(0, recv(fd, reply, sizeof(reply), 0));
}

/**
 * @brief A temporary gap retains the source and output, clears meters, and resumes without preparation.
 */
static void receive_grace(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    selected = OUTPUT_NETWORK;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    for (unsigned i = 0; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        send_pcm(fd, 1);
    }

    for (unsigned i = 0; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    }

    unsigned generation = server.stream.generation;

    now += 250;

    pump();
    TEST_ASSERT_TRUE(aria_server_active(&server, now));

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.disconnects);
#endif
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(1, cleared);
    TEST_ASSERT_EQUAL_HEX8(0, submitted[0]);
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(1, cleared);

    now += 1000;

    for (unsigned i = 0; i < ARIA_PREBUFFER_MESSAGES - 1; ++i)
    {
        send_pcm(fd, (uint8_t)(7 + i));
        aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
        TEST_ASSERT_EQUAL_HEX8(0, submitted[0]);
        TEST_ASSERT_EQUAL_UINT(i + 1, server.stream.count);
        TEST_ASSERT_EQUAL_UINT(1, cleared);
    }

    send_pcm(fd, 99);

    write_result = 0;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(ARIA_PREBUFFER_MESSAGES, server.stream.count);
    TEST_ASSERT_EQUAL_UINT(ARIA_PREBUFFER_MESSAGES, captured);

    write_result = 1;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(ARIA_PREBUFFER_MESSAGES - 1, server.stream.count);
    TEST_ASSERT_EQUAL_UINT(generation, server.stream.generation);
    TEST_ASSERT_EQUAL_INT(1, prepared);
    TEST_ASSERT_EQUAL_INT(0, stopped);
    TEST_ASSERT_EQUAL_HEX8(7, submitted[0]);

    for (unsigned i = 1; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
        TEST_ASSERT_EQUAL_HEX8(i == ARIA_PREBUFFER_MESSAGES - 1 ? 99 : 7 + i, submitted[0]);
    }

    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_TRUE(playback.buffering);
    TEST_ASSERT_EQUAL_UINT(2, cleared);
    TEST_ASSERT_TRUE(aria_playback_stop(&playback) == 0);
    TEST_ASSERT_FALSE(playback.buffering);

    now += ARIA_IDLE_MS;

    pump();
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_FALSE(aria_server_active(&server, now));

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.disconnects);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("PCM receive timeout", server.diagnostics.last_disconnect);
#endif
}

/**
 * @brief Separate input starvation, queued-PCM output starvation, busy output and errors.
 */
#if STROOM_DIAGNOSTICS
static void output_diagnostics(void)
{
    AriaDiagnostics d = { 0 };

    aria_diagnostics_output(&d, 100, 1, 0, 0);
    TEST_ASSERT_EQUAL_UINT(0, d.empty_with_pcm);
    aria_diagnostics_output(&d, 120, 1, 0, 0);
    TEST_ASSERT_EQUAL_UINT(1, d.empty_with_pcm);
    aria_diagnostics_output(&d, 140, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT(1, d.empty_without_pcm);
    TEST_ASSERT_EQUAL_UINT(1, d.silence_writes);
    aria_diagnostics_output(&d, 150, 1, 4096, 1);
    TEST_ASSERT_EQUAL_UINT(1, d.busy_writes);
    TEST_ASSERT_EQUAL_UINT(140, d.last_write_at);
    aria_diagnostics_output(&d, 160, 1, -1, -1);
    TEST_ASSERT_EQUAL_UINT(1, d.output_errors);
    TEST_ASSERT_EQUAL_UINT(1, d.empty_with_pcm);
    aria_diagnostics_output(&d, 240, 1, 0, 0);
    TEST_ASSERT_EQUAL_UINT(100, d.max_write_gap);
    TEST_ASSERT_EQUAL_UINT(3, d.pcm_writes);
    aria_diagnostics_session(&d);
    TEST_ASSERT_EQUAL_INT(-1, d.sound_queued);
    aria_diagnostics_output(&d, 1000, 1, 0, 0);
    TEST_ASSERT_EQUAL_UINT(100, d.max_write_gap);
    TEST_ASSERT_EQUAL_UINT(2, d.empty_with_pcm);
    aria_diagnostics_output(&d, 1010, 1, 4096, 1);
    aria_diagnostics_output(&d, 1011, 1, -1, 1); /* Local rejection: no new observation. */
    TEST_ASSERT_EQUAL_INT(4096, d.sound_queued);
    TEST_ASSERT_EQUAL_UINT(2, d.empty_with_pcm);
}
#endif

/**
 * @brief UDP diagnostics work with every application client slot occupied.
 */
#if STROOM_DIAGNOSTICS
static void udp_diagnostics(void)
{
    char reply[4096];

    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        upgrade("/metadata", reply);
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd);

    peers[peer_count++] = fd;

    TEST_ASSERT_EQUAL_INT(0, nonblocking(fd));

    struct sockaddr_in address = { 0 };

    address.sin_family      = AF_INET;
    address.sin_port        = htons(ARIA_DISCOVERY_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    snprintf(server.diagnostics.last_disconnect, sizeof(server.diagnostics.last_disconnect), "sound output failed");

    server.diagnostics.disconnects = 7;

    unsigned   generation = server.stream.generation;
    const char query[]    = DIAGNOSTIC_QUERY;

    TEST_ASSERT_EQUAL_INT(sizeof(query) - 1, sendto(fd, query, sizeof(query) - 1, 0, (struct sockaddr*)&address, sizeof(address)));
    pump();

    int n = recv(fd, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, n);

    reply[n] = 0;

    TEST_ASSERT_EQUAL_CHAR('}', reply[n - 1]);
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"diagnosticVersion\":1"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"audioDiagnosticVersion\":1"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"maxArtworkMs\":"));

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"overflowPolicy\":\"backpressure\""));
#endif
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"graceMs\":3000"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"clients\":6"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"disconnects\":7"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "sound output failed"));
    TEST_ASSERT_EQUAL_UINT(generation, server.stream.generation);

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.discovery_matched);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.discovery_replied);
#endif
    const char raw[] = "{\"title\":false}";

    server.diagnostics.metadata_request[0] = TRACK_METADATA_INVALID;

    memcpy(server.diagnostics.metadata_request + 1, raw, sizeof(raw) - 1);

    server.diagnostics.metadata_request_size = sizeof(raw);

    const char meta_query[] = DIAGNOSTIC_METADATA_QUERY;

    TEST_ASSERT_EQUAL_INT(sizeof(meta_query) - 1, sendto(fd, meta_query, sizeof(meta_query) - 1, 0, (struct sockaddr*)&address, sizeof(address)));
    pump();

    n = recv(fd, reply, sizeof(reply), 0);

    TEST_ASSERT_EQUAL_INT(sizeof(raw), n);
    TEST_ASSERT_EQUAL_UINT8(TRACK_METADATA_INVALID, reply[0]);
    TEST_ASSERT_EQUAL_MEMORY(raw, reply + 1, sizeof(raw) - 1);

    const char driver_query[] = DIAGNOSTIC_DRIVER_QUERY;

    TEST_ASSERT_EQUAL_INT(sizeof(driver_query) - 1, sendto(fd, driver_query, sizeof(driver_query) - 1, 0, (struct sockaddr*)&address, sizeof(address)));
    pump();

    n = recv(fd, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "\"driverDiagnosticVersion\":1"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"rxAllocFail\":123"));
    aria_diagnostic_capture_record(&server.diagnostics.diagnostic_capture, (AriaDiagnosticCaptureRecord){ .at = 100, .kind = DIAGNOSTIC_CAPTURE_SAMPLE });
    aria_diagnostic_capture_output(&server.diagnostics.diagnostic_capture, 100, server.stream.generation, 0);
    aria_diagnostic_capture_tick(&server.diagnostics.diagnostic_capture, 2100);

    const char diagnostic_query[] = "STROOM_DIAGNOSTIC 1 0";

    TEST_ASSERT_EQUAL_INT(sizeof(diagnostic_query) - 1, sendto(fd, diagnostic_query, sizeof(diagnostic_query) - 1, 0, (struct sockaddr*)&address, sizeof(address)));
    pump();

    n = recv(fd, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "\"diagnosticVersion\":1"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"records\":["));
    TEST_ASSERT_EQUAL_UINT(generation, server.stream.generation);
    strcpy(server.metadata.artwork_url, "http://192.168.1.33:8090/artwork.jpg?v=1");
    strcpy(server.diagnostics.artwork_fetch, "DOWNLOADED; HTTP 200; BYTES 1234");
    strcpy(server.diagnostics.artwork_display, "JPEG DECODE FAILED");
    aria_diagnostics_cover(&server.diagnostics, (AriaDiagnosticCaptureRecord){ .at = 6, .kind = DIAGNOSTIC_CAPTURE_COVER, .data = { [DIAGNOSTIC_CAPTURE_COVER_PHASE] = DIAGNOSTIC_ARTWORK_DECODE_END, [DIAGNOSTIC_CAPTURE_COVER_DURATION_MS] = 12, [DIAGNOSTIC_CAPTURE_COVER_METADATA_REVISION] = 7, [DIAGNOSTIC_CAPTURE_COVER_VALUE0] = 640, [DIAGNOSTIC_CAPTURE_COVER_VALUE1] = 480, [DIAGNOSTIC_CAPTURE_COVER_VALUE2] = 257 } });
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.cover_phases[DIAGNOSTIC_ARTWORK_DECODE_END - 1].count);
    aria_diagnostics_cover(&server.diagnostics, (AriaDiagnosticCaptureRecord){ .at = 10, .data = { [DIAGNOSTIC_CAPTURE_COVER_PHASE] = DIAGNOSTIC_ARTWORK_DECODE_END, [DIAGNOSTIC_CAPTURE_COVER_DURATION_MS] = 4, [DIAGNOSTIC_CAPTURE_COVER_METADATA_REVISION] = 7, [DIAGNOSTIC_CAPTURE_COVER_VALUE0] = 640, [DIAGNOSTIC_CAPTURE_COVER_VALUE1] = 480, [DIAGNOSTIC_CAPTURE_COVER_VALUE2] = 257 } });
    TEST_ASSERT_EQUAL_UINT(12, server.diagnostics.cover_phases[DIAGNOSTIC_ARTWORK_DECODE_END - 1].maximum);

    const char art_query[] = DIAGNOSTIC_ARTWORK_QUERY;

    TEST_ASSERT_EQUAL_INT(sizeof(art_query) - 1, sendto(fd, art_query, sizeof(art_query) - 1, 0, (struct sockaddr*)&address, sizeof(address)));
    pump();

    n = recv(fd, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "\"artworkDiagnosticVersion\":1"));
    TEST_ASSERT_NOT_NULL(strstr(reply, server.metadata.artwork_url));

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_NOT_NULL(strstr(reply, server.diagnostics.artwork_fetch));
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_NOT_NULL(strstr(reply, server.diagnostics.artwork_display));
#endif
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"coverTimingRevision\":7"));
    TEST_ASSERT_NOT_NULL(strstr(reply, "[2,2,12,6,10,640,480,257]"));
    TEST_ASSERT_EQUAL_CHAR('}', reply[n - 1]);
    aria_diagnostics_cover(&server.diagnostics, (AriaDiagnosticCaptureRecord){ .at = 20, .data = { [DIAGNOSTIC_CAPTURE_COVER_PHASE] = DIAGNOSTIC_ARTWORK_DECODE_BEGIN, [DIAGNOSTIC_CAPTURE_COVER_DURATION_MS] = 0, [DIAGNOSTIC_CAPTURE_COVER_METADATA_REVISION] = 8 } });
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.cover_phases[DIAGNOSTIC_ARTWORK_DECODE_END - 1].count);
    TEST_ASSERT_EQUAL_UINT(8, server.diagnostics.cover_revision);
    aria_diagnostics_cover(&server.diagnostics, (AriaDiagnosticCaptureRecord){ .data = { [DIAGNOSTIC_CAPTURE_COVER_PHASE] = 0, [DIAGNOSTIC_CAPTURE_COVER_DURATION_MS] = 0, [DIAGNOSTIC_CAPTURE_COVER_METADATA_REVISION] = 9 } });
    aria_diagnostics_cover(&server.diagnostics, (AriaDiagnosticCaptureRecord){ .data = { [DIAGNOSTIC_CAPTURE_COVER_PHASE] = DIAGNOSTIC_ARTWORK_SOCKET_ERROR + 1, [DIAGNOSTIC_CAPTURE_COVER_DURATION_MS] = 0, [DIAGNOSTIC_CAPTURE_COVER_METADATA_REVISION] = 9 } });
    aria_diagnostics_cover(&server.diagnostics, (AriaDiagnosticCaptureRecord){ .data = { [DIAGNOSTIC_CAPTURE_COVER_PHASE] = DIAGNOSTIC_ARTWORK_CLOSE, [DIAGNOSTIC_CAPTURE_COVER_DURATION_MS] = 0, [DIAGNOSTIC_CAPTURE_COVER_METADATA_REVISION] = 0 } });
    TEST_ASSERT_EQUAL_UINT(8, server.diagnostics.cover_revision);
    aria_diagnostics_session(&server.diagnostics);
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.cover_revision);
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.cover_phases[DIAGNOSTIC_ARTWORK_DECODE_BEGIN - 1].count);
    /* Maximum-width rows and metadata must fit a complete UDP JSON response. */

    for (unsigned i = 1; i <= DIAGNOSTIC_ARTWORK_SOCKET_ERROR; ++i)
    {
        aria_diagnostics_cover(&server.diagnostics, (AriaDiagnosticCaptureRecord){ .at = UINT32_MAX, .data = { [DIAGNOSTIC_CAPTURE_COVER_PHASE] = i, [DIAGNOSTIC_CAPTURE_COVER_DURATION_MS] = UINT32_MAX, [DIAGNOSTIC_CAPTURE_COVER_METADATA_REVISION] = UINT32_MAX, [DIAGNOSTIC_CAPTURE_COVER_VALUE0] = UINT32_MAX, [DIAGNOSTIC_CAPTURE_COVER_VALUE1] = UINT32_MAX, [DIAGNOSTIC_CAPTURE_COVER_VALUE2] = UINT32_MAX } });

        server.diagnostics.cover_phases[i - 1].count = UINT32_MAX;
    }

    memset(server.metadata.title, 't', sizeof(server.metadata.title) - 1);
    memset(server.metadata.artist, 'a', sizeof(server.metadata.artist) - 1);
    memset(server.metadata.album, 'b', sizeof(server.metadata.album) - 1);
    TEST_ASSERT_EQUAL_INT(sizeof(art_query) - 1, sendto(fd, art_query, sizeof(art_query) - 1, 0, (struct sockaddr*)&address, sizeof(address)));
    pump();

    n = recv(fd, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "[18,4294967295,4294967295,0,4294967295,4294967295,4294967295,4294967295]"));
    TEST_ASSERT_EQUAL_CHAR('}', reply[n - 1]);

    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, server.clients[i].fd);
    }
}
#endif

/** @brief Find the reserved audio client to anchor checks to its actual opening time. */
static AriaClient* audio_client(void)
{
    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        if (server.audio_fd >= 0 && server.clients[i].fd == server.audio_fd)
        {
            return &server.clients[i];
        }
    }

    TEST_FAIL_MESSAGE("No reserved audio client");

    return NULL;
}

/**
 * @brief An audio reservation expires after 30 seconds despite control traffic and partial PCM.
 */
static void first_pcm_deadline(void)
{
    now = UINT32_MAX - 10000;

    char        reply[2048];
    int         fd     = upgrade("/audio", reply);
    AriaClient* client = audio_client();
    uint32_t    opened = client->audio_opened;

    now = opened + 20000;

    pump();
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, server.audio_fd);

    uint8_t  wire[16];
    unsigned n = masked(wire, ARIA_WS_PING, 1, (const uint8_t*)"x", 1);

    TEST_ASSERT_EQUAL_INT(n, send(fd, wire, n, 0));
    pump();
    TEST_ASSERT_EQUAL_INT(3, recv(fd, reply, sizeof(reply), 0));

    n = masked(wire, ARIA_WS_BINARY, 0, (const uint8_t*)"x", 1);

    TEST_ASSERT_EQUAL_INT(n, send(fd, wire, n, 0));

    now = opened + ARIA_FIRST_PCM_TIMEOUT_MS - 40;

    pump_for(40);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, server.audio_fd);
    TEST_ASSERT_EQUAL_UINT(0, server.stream.received);

    now = opened + ARIA_FIRST_PCM_TIMEOUT_MS;

    pump();
    TEST_ASSERT_EQUAL_INT(-1, server.audio_fd);

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("first PCM timeout", server.diagnostics.last_disconnect);
#endif
    upgrade("/audio", reply);
    TEST_ASSERT_NOT_NULL(strstr(reply, "101 Switching Protocols"));
}

/**
 * @brief A first frame near the startup deadline switches to the normal three-second grace.
 */
static void first_pcm_starts_grace(void)
{
    char reply[2048];
    int  fd = upgrade("/audio", reply);

    now = audio_client()->audio_opened + ARIA_FIRST_PCM_TIMEOUT_MS - 100;

    send_pcm(fd, 5);
    TEST_ASSERT_TRUE(aria_server_active(&server, now));

    now += 200;

    pump();
    TEST_ASSERT_TRUE(aria_server_active(&server, now));

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.disconnects);
#endif
    now += ARIA_IDLE_MS;

    pump();

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("PCM receive timeout", server.diagnostics.last_disconnect);
#endif
}

/**
 * @brief A wrapping receipt counter preserves PCM liveness, deadlines, and reconnect state.
 */
static void pcm_counter_wraparound(void)
{
    char reply[2048];
    int  fd = upgrade("/audio", reply);

    TEST_ASSERT_FALSE(server.stream.has_pcm);

    now                    = audio_client()->audio_opened + ARIA_FIRST_PCM_TIMEOUT_MS - 100;
    server.stream.received = UINT_MAX;

    send_pcm(fd, 5);
    TEST_ASSERT_EQUAL_UINT(0, server.stream.received);
    TEST_ASSERT_TRUE(server.stream.has_pcm);
    TEST_ASSERT_TRUE(aria_server_active(&server, now));

    now += 200;

    pump();
    TEST_ASSERT_GREATER_OR_EQUAL(0, server.audio_fd);
    TEST_ASSERT_TRUE(aria_server_active(&server, now));

    now += ARIA_IDLE_MS;
#if STROOM_DIAGNOSTICS
    uint32_t gap = now - server.stream.last_pcm;
#endif
    pump();
    TEST_ASSERT_EQUAL_INT(-1, server.audio_fd);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_STRING("PCM receive timeout", server.diagnostics.last_disconnect);
    TEST_ASSERT_EQUAL_UINT(gap, server.diagnostics.last_disconnect_gap);
#endif

    upgrade("/audio", reply);
    TEST_ASSERT_FALSE(server.stream.has_pcm);
    TEST_ASSERT_FALSE(aria_server_active(&server, now));
}

/**
 * @brief POST split metadata bytes and check the HTTP status.
 * @param body Complete JSON document.
 * @param status Expected response status text.
 */
static void post_metadata(const char* body, const char* status)
{
    int  fd = connect_client();
    char header[256];
    int  n = snprintf(header, sizeof(header), "POST /metadata HTTP/1.1\r\nContent-Length: %u\r\n\r\n", (unsigned)strlen(body));

    TEST_ASSERT_EQUAL_INT(n, send(fd, header, (size_t)n, 0));

    unsigned half = (unsigned)strlen(body) / 2;

    TEST_ASSERT_EQUAL_INT((int)half, send(fd, body, half, 0));
    pump();
    TEST_ASSERT_EQUAL_INT((int)(strlen(body) - half), send(fd, body + half, strlen(body) - half, 0));
    pump();

    char reply[2048];
    int  got = (int)recv(fd, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, got);

    reply[got] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, status));
}

/**
 * @brief HTTP updates reach subscribers, preserve absent labels and clear on disconnect.
 */
static void metadata_transport(void)
{
    post_metadata("{\"data\":{\"title\":\"Track\",\"artist\":\"Artist\",\"album\":\"Album\"}}", "200 OK");
    TEST_ASSERT_EQUAL_STRING("Track", server.metadata.title);

    char reply[2048];

    upgrade("/audio", reply);
    TEST_ASSERT_EQUAL_STRING("Track", server.metadata.title);

    int subscriber = upgrade("/metadata", reply);

    TEST_ASSERT_NOT_NULL(strstr(reply, "Track"));
    post_metadata("{\"data\":{\"title\":\"Next\"}}", "200 OK");
    TEST_ASSERT_EQUAL_STRING("Artist", server.metadata.artist);
    pump();

    int n = (int)recv(subscriber, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "Next"));
    post_metadata("{\"title\":false}", "400 Bad Request");
    TEST_ASSERT_EQUAL_STRING("Next", server.metadata.title);

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT8(TRACK_METADATA_INVALID, server.diagnostics.metadata_request[0]);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_MEMORY("{\"title\":false}", server.diagnostics.metadata_request + 1, server.diagnostics.metadata_request_size - 1);
#endif
    uint8_t    wire[256];
    const char update[] = "{\"type\":\"update\",\"data\":{\"artist\":null}}";
    unsigned   size     = masked(wire, ARIA_WS_TEXT, 1, (const uint8_t*)update, sizeof(update) - 1);

    TEST_ASSERT_EQUAL_INT((int)size, send(subscriber, wire, size, 0));
    pump();
    TEST_ASSERT_EQUAL_STRING("", server.metadata.artist);
    TEST_ASSERT_EQUAL_STRING("Album", server.metadata.album);

    n = (int)recv(subscriber, reply, sizeof(reply) - 1, 0);

    TEST_ASSERT_GREATER_THAN(0, n);

    reply[n] = 0;

    TEST_ASSERT_NOT_NULL(strstr(reply, "ack"));
    aria_server_disconnect(&server, "test");
    TEST_ASSERT_EQUAL_STRING("", server.metadata.title);
    TEST_ASSERT_EQUAL_STRING("", server.metadata.album);
}

/**
 * @brief Accept sender metadata and ignore unsupported timing refreshes.
 */
static void official_metadata(void)
{
    char reply[2048];

    upgrade("/audio", reply);
    post_metadata("{\"data\":{\"title\":\"Lovesick\",\"artist\":\"Darci\",\"album\":\"Lovesick\",\"artworkUrl\":\"http://192.168.1.34:8090/artwork.jpg\",\"durationMs\":183000,\"positionMs\":157154,\"isPlaying\":true}}", "200 OK");
    TEST_ASSERT_EQUAL_STRING("Lovesick", server.metadata.title);
    TEST_ASSERT_EQUAL_STRING("Darci", server.metadata.artist);
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.34:8090/artwork.jpg", server.metadata.artwork_url);
    post_metadata("{\"data\":{\"positionMs\":158154,\"isPlaying\":true}}", "200 OK");
    TEST_ASSERT_EQUAL_STRING("Lovesick", server.metadata.title);
    TEST_ASSERT_EQUAL_STRING("Darci", server.metadata.artist);
}

/**
 * @brief Reject another address's metadata while audio has an owner.
 */
static void metadata_sender_ownership(void)
{
    char reply[2048];

    upgrade("/audio", reply);

    for (unsigned i = 0; i < ARIA_CLIENTS; ++i)
    {
        if (server.clients[i].fd == server.audio_fd)
        {
            server.clients[i].peer_address = htonl(0x7f000002);
        }
    }

    post_metadata("{\"title\":\"Wrong sender\"}", "400 Bad Request");
    TEST_ASSERT_EQUAL_STRING("", server.metadata.title);
    aria_server_disconnect(&server, "test");

    selected = OUTPUT_CD;

    pump();
    post_metadata("{\"title\":\"CD owns audio\"}", "400 Bad Request");
    TEST_ASSERT_EQUAL_STRING("", server.metadata.title);
}

/**
 * @brief Require a selected audio peer before reporting a usable IOP diagnostic.
 */
#if STROOM_DIAGNOSTICS
static void diagnostic_selection_status(void)
{
    char reply[2048];

    upgrade("/audio", reply);

    diagnostic_reply.status = 1;
    diagnostic_reply.abi    = DIAGNOSTIC_IOP_ABI;

    aria_diagnostic_poll(&server, now + 100);

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_INT(-3, server.diagnostics.diagnostic_capture.iop_status);
#endif
    diagnostic_reply.peer               = 0x7f000001;
    diagnostic_reply.port               = 43210;
    diagnostic_reply.count              = 1;
    diagnostic_reply.records[0].kind    = DIAGNOSTIC_IOP_SACK_STATE;
    diagnostic_reply.records[0].data[0] = 1;
    diagnostic_reply.records[0].data[1] = 1;

    aria_diagnostic_poll(&server, now + 200);

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_INT(1, server.diagnostics.diagnostic_capture.iop_status);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.diagnostic_capture.iop_records);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.diagnostic_capture.sack_known);
#endif

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(1, server.diagnostics.diagnostic_capture.sack_negotiated);
#endif
    aria_diagnostic_capture_session(&server.diagnostics.diagnostic_capture, server.stream.generation + 1);

#if STROOM_DIAGNOSTICS
    TEST_ASSERT_EQUAL_UINT(0, server.diagnostics.diagnostic_capture.sack_known);
#endif
}
#endif

/**
 * @brief Keep protocol statistics available with diagnostics excluded.
 */
static void protocol_statistics(void)
{
    char json[2048];

    server.stream.received = 7;
    server.stream.count    = 2;

    aria_statistics(&server, json, sizeof(json));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"receivedFrames\":7"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"bufferedFrames\":2"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"queuedFrames\":2"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"droppedFrames\":0"));
#if !STROOM_DIAGNOSTICS
    TEST_ASSERT_NULL(strstr(json, "diagnosticVersion"));

    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd);
    TEST_ASSERT_EQUAL_INT(0, nonblocking(fd));

    struct sockaddr_in peer = { .sin_family = AF_INET, .sin_port = htons(ARIA_DISCOVERY_PORT), .sin_addr = { .s_addr = htonl(INADDR_LOOPBACK) } };

    TEST_ASSERT_GREATER_THAN_INT(0, sendto(fd, DIAGNOSTIC_QUERY, strlen(DIAGNOSTIC_QUERY), 0, (struct sockaddr*)&peer, sizeof(peer)));
    pump();
    TEST_ASSERT_EQUAL_INT(-1, recv(fd, json, sizeof(json), 0));
    TEST_ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
    close(fd);
#endif
}

/** @brief End the TCP stream without any application completion message. */
static void finish_stream(int fd)
{
    TEST_ASSERT_EQUAL_INT(0, shutdown(fd, SHUT_WR));
    pump_for(20);
}

/** @brief Verify the transport is closed and playback has released its session. */
static void expect_finished(int fd)
{
    uint8_t reply[128];

    pump();
    TEST_ASSERT_FALSE(aria_server_active(&server, now));
    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
    TEST_ASSERT_EQUAL_INT(0, recv(fd, reply, sizeof(reply), 0));
}

/** @brief Short streams bypass prebuffering, preserve busy PCM, and finish only after lead-out. */
static void finish_short_stream(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    selected = OUTPUT_NETWORK;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    send_pcm(fd, 7);
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(0, captured);
    finish_stream(fd);
    TEST_ASSERT_TRUE(server.stream.ending);

    write_result = 0;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(1, server.stream.count);
    TEST_ASSERT_EQUAL_UINT(0, captured);

    write_result = 1;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(1, captured);
    TEST_ASSERT_EQUAL_HEX8(7, submitted[0]);

    write_result = 0;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(0, playback.tail_blocks);

    write_result = 1;

    for (unsigned i = 0; i < OUTPUT_LEAD_OUT_BLOCKS; ++i)
    {
        aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
        TEST_ASSERT_FALSE(server.stream.finished);
        TEST_ASSERT_EQUAL_INT(0, stopped);
        TEST_ASSERT_EQUAL_UINT(OUTPUT_SILENCE_BYTES, submitted_bytes);
        TEST_ASSERT_EQUAL_HEX8(0, submitted[0]);
    }

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_TRUE(server.stream.finished);
    TEST_ASSERT_EQUAL_INT(1, stopped);
    expect_finished(fd);
    upgrade("/audio", reply);
    TEST_ASSERT_FALSE(server.stream.ending);
    TEST_ASSERT_FALSE(server.stream.finished);
}

/** @brief Ending while rebuffering releases all remaining packets in order. */
static void finish_rebuffered_stream(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    selected = OUTPUT_NETWORK;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    for (unsigned i = 0; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        send_pcm(fd, (uint8_t)i);
    }

    for (unsigned i = 0; i < ARIA_PREBUFFER_MESSAGES; ++i)
    {
        aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    }

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_TRUE(playback.buffering);
    send_pcm(fd, 91);
    send_pcm(fd, 92);
    finish_stream(fd);

    for (unsigned i = 0; i < 2; ++i)
    {
        aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
        TEST_ASSERT_EQUAL_HEX8(91 + i, submitted[0]);
    }

    TEST_ASSERT_EQUAL_UINT(ARIA_PREBUFFER_MESSAGES + 2, captured);

    for (unsigned i = 0; i <= OUTPUT_LEAD_OUT_BLOCKS; ++i)
    {
        aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    }

    expect_finished(fd);
}

/** @brief Empty input completes without opening sound output. */
static void finish_empty_stream(void)
{
    char reply[2048];
    int  fd = upgrade("/audio", reply);

    finish_stream(fd);
    expect_finished(fd);
    TEST_ASSERT_EQUAL_INT(0, initialized);
    TEST_ASSERT_EQUAL_INT(0, prepared);
}

/** @brief Listening consumes its final packet for its full duration, without sound RPCs. */
static void finish_listening_stream(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    server.stream.listening = 1;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    send_pcm(fd, 7);
    finish_stream(fd);
    memset(submitted, 7, sizeof(submitted));
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(1, captured);
    ++now;
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_FALSE(server.stream.finished);

    now += ARIA_PCM_MESSAGE_MS;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    expect_finished(fd);
    TEST_ASSERT_EQUAL_INT(0, prepared);
    TEST_ASSERT_EQUAL_INT(0, stopped);
}

/** @brief Source cancellation drops an already disconnected stream immediately. */
static void cancel_drain(void)
{
    char reply[2048];
    int  fd = upgrade("/audio", reply);

    send_pcm(fd, 7);
    finish_stream(fd);
    TEST_ASSERT_TRUE(aria_server_active(&server, now));
    aria_server_disconnect(&server, "source ownership changed");
    TEST_ASSERT_FALSE(aria_server_active(&server, now));
    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
}

/** @brief A replacement connection must not inherit the old stream's tail. */
static void replace_drain(void)
{
    char reply[2048];
    int  fd = upgrade("/audio", reply);

    post_metadata("{\"title\":\"Old track\",\"artist\":\"Old artist\",\"album\":\"Old album\",\"artwork_url\":\"http://127.0.0.1/old.jpg\"}", "200 OK");
    send_pcm(fd, 7);

    unsigned metadata_revision = server.metadata_revision;

    finish_stream(fd);
    TEST_ASSERT_EQUAL_STRING("", server.metadata.title);
    TEST_ASSERT_EQUAL_STRING("", server.metadata.artist);
    TEST_ASSERT_EQUAL_STRING("", server.metadata.album);
    TEST_ASSERT_EQUAL_STRING("", server.metadata.artwork_url);
    TEST_ASSERT_EQUAL_UINT(metadata_revision + 1, server.metadata_revision);
    TEST_ASSERT_EQUAL_UINT(1, server.stream.count);

    unsigned generation = server.stream.generation;

    fd = upgrade("/audio", reply);

    TEST_ASSERT_NOT_EQUAL(generation, server.stream.generation);
    TEST_ASSERT_FALSE(server.stream.ending);
    TEST_ASSERT_EQUAL_STRING("", server.metadata.title);
    TEST_ASSERT_EQUAL_STRING("", server.metadata.artwork_url);
    TEST_ASSERT_EQUAL_UINT(0, server.stream.count);
    send_pcm(fd, 8);
    TEST_ASSERT_EQUAL_HEX8(8, server.stream.pcm[server.stream.read][0]);
}

/** @brief Metadata sent during a tail belongs to the next connection, not tail cleanup. */
static void drain_metadata(int finish_old, int different_sender)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    selected = OUTPUT_NETWORK;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    send_pcm(fd, 7);
    finish_stream(fd);
    post_metadata("{\"title\":\"Upcoming track\",\"artwork_url\":\"http://127.0.0.1/new.jpg\"}", "200 OK");

    unsigned revision = server.metadata_revision;

    if (finish_old)
    {
        for (unsigned i = 0; i < OUTPUT_LEAD_OUT_BLOCKS + 2; ++i)
        {
            aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
        }

        expect_finished(fd);
        TEST_ASSERT_EQUAL_UINT(1, captured);
        TEST_ASSERT_EQUAL_UINT(revision, server.metadata_revision);
        TEST_ASSERT_EQUAL_STRING("Upcoming track", server.metadata.title);
    }

    if (different_sender)
    {
        // Model metadata from another address, as in metadata_sender_ownership.
        server.metadata_peer = htonl(0x7f000002);
    }

    fd = upgrade("/audio", reply);

    send_pcm(fd, 8);
    TEST_ASSERT_EQUAL_STRING(different_sender ? "" : "Upcoming track", server.metadata.title);
    TEST_ASSERT_EQUAL_STRING(different_sender ? "" : "http://127.0.0.1/new.jpg", server.metadata.artwork_url);
}

/** @brief A new connection can consume pre-sent metadata before the old tail completes. */
static void metadata_reconnect_during_drain(void)
{
    drain_metadata(0, 0);
}

/** @brief Completing the old tail must not erase pre-sent metadata. */
static void metadata_reconnect_after_drain(void)
{
    drain_metadata(1, 0);
}

/** @brief Another sender cannot inherit pre-sent metadata from the disconnected sender. */
static void metadata_different_sender_after_drain(void)
{
    drain_metadata(1, 1);
}

/** @brief Loss without TCP EOF releases a short queue when the receive deadline expires. */
static void receive_timeout_drains(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    selected = OUTPUT_NETWORK;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    send_pcm(fd, 9);

    now += ARIA_IDLE_MS;

    pump_for(20);
    TEST_ASSERT_FALSE(server.stream.connected);
    TEST_ASSERT_TRUE(server.stream.ending);
    TEST_ASSERT_TRUE(aria_server_active(&server, now));
    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_EQUAL_UINT(1, captured);
    TEST_ASSERT_EQUAL_HEX8(9, submitted[0]);
}

/** @brief A reset socket retains complete PCM already received. */
static void reset_drains(void)
{
    char reply[2048];
    int  fd = upgrade("/audio", reply);

    send_pcm(fd, 9);

    struct linger reset = { .l_onoff = 1, .l_linger = 0 };

    TEST_ASSERT_EQUAL_INT(0, setsockopt(fd, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)));
    close(fd);
    pump_for(20);
    TEST_ASSERT_TRUE(server.stream.ending);
    TEST_ASSERT_EQUAL_UINT(1, server.stream.count);
}

/** @brief WebSocket closure preserves complete PCM but discards an incomplete message. */
static void websocket_tail(void)
{
    char reply[2048];
    int  fd = upgrade("/audio", reply);

    send_pcm(fd, 42);

    uint8_t  wire[64];
    unsigned size = masked(wire, 2, 0, (const uint8_t*)"partial", 7);

    TEST_ASSERT_EQUAL_INT(size, send(fd, wire, size, 0));

    size = masked(wire, 8, 1, NULL, 0);

    TEST_ASSERT_EQUAL_INT(size, send(fd, wire, size, 0));
    pump_for(30);
    TEST_ASSERT_FALSE(server.stream.connected);
    TEST_ASSERT_TRUE(server.stream.ending);
    TEST_ASSERT_EQUAL_UINT(1, server.stream.count);
    TEST_ASSERT_EQUAL_HEX8(42, server.stream.pcm[server.stream.read][0]);
}

/** @brief A stuck drain is bounded even across millisecond wraparound. */
static void finish_timeout(void)
{
    now = UINT32_MAX - 1000;

    char reply[2048];
    int  fd = upgrade("/audio", reply);

    send_pcm(fd, 7);
    finish_stream(fd);
    TEST_ASSERT_TRUE(aria_server_active(&server, now));

    now += ARIA_IDLE_MS;

    pump();
    TEST_ASSERT_FALSE(server.stream.connected);
    TEST_ASSERT_EQUAL_UINT(0, captured);
}

/** @brief Failed sound stop cancels the disconnected session. */
static void finish_stop_failure(void)
{
    char reply[2048], error[96] = { 0 };
    int  fd = upgrade("/audio", reply);

    selected = OUTPUT_NETWORK;

    OutputRuntime runtime = { .running = &running, .error = error, .capacity = sizeof(error) };

    send_pcm(fd, 7);
    finish_stream(fd);

    for (unsigned i = 0; i <= OUTPUT_LEAD_OUT_BLOCKS; ++i)
    {
        aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    }

    stop_result = 0;

    aria_playback_step(&playback, &server.stream, ARIA_DIAGNOSTICS(&server), &runtime, now, capture, NULL);
    TEST_ASSERT_FALSE(server.stream.connected);
    TEST_ASSERT_EQUAL_STRING("STREAM SOUND STOP ERROR", error);
}

/**
 * @brief Run protocol and sound-routing regressions.
 * @return Failed case count.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(finish_short_stream);
    RUN_TEST(finish_rebuffered_stream);
    RUN_TEST(finish_empty_stream);
    RUN_TEST(finish_listening_stream);
    RUN_TEST(cancel_drain);
    RUN_TEST(receive_timeout_drains);
    RUN_TEST(reset_drains);
    RUN_TEST(websocket_tail);
    RUN_TEST(replace_drain);
    RUN_TEST(metadata_reconnect_during_drain);
    RUN_TEST(metadata_reconnect_after_drain);
    RUN_TEST(metadata_different_sender_after_drain);
    RUN_TEST(finish_timeout);
    RUN_TEST(finish_stop_failure);
#if STROOM_DIAGNOSTICS
    RUN_TEST(diagnostic_selection_status);
#endif
    RUN_TEST(protocol_statistics);
    RUN_TEST(metadata_transport);
    RUN_TEST(official_metadata);
    RUN_TEST(metadata_sender_ownership);
    RUN_TEST(first_pcm_deadline);
    RUN_TEST(first_pcm_starts_grace);
    RUN_TEST(pcm_counter_wraparound);
    RUN_TEST(heartbeat);
    RUN_TEST(heartbeat_wraparound);
    RUN_TEST(receive_grace);
#if STROOM_DIAGNOSTICS
    RUN_TEST(udp_diagnostics);
#endif
#if STROOM_DIAGNOSTICS
    RUN_TEST(output_diagnostics);
#endif
    RUN_TEST(websocket);
    RUN_TEST(websocket_close_terminates);
    RUN_TEST(close_before_pcm);
    RUN_TEST(statistics_after_close);
    RUN_TEST(receiver);
    RUN_TEST(sound_delivery);
    RUN_TEST(listening_delivery);
    RUN_TEST(listening_device_names);
    RUN_TEST(listening_requires_negotiation_before_pcm);
    RUN_TEST(startup_reserve);
    RUN_TEST(output_error);
    RUN_TEST(protocol_limits);
    RUN_TEST(stop_retry);
    RUN_TEST(malformed_requests);
    RUN_TEST(listener_failure);
    RUN_TEST(startup_failures);

    return UNITY_END();
}
