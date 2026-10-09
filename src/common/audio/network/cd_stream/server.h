#pragma once

#include "contracts/cd_stream.h"
#include "platform/network/tcp_state_wire.h"
#include "platform/network/rpc/driver_stats.h"
#include <stdint.h>

#define CD_STREAM_CLIENTS       4
#define CD_STREAM_BLOCKS        150 /* Two seconds at 75 resampled CD sectors per second. */
#define CD_STREAM_REQUEST_BYTES 64
#define CD_STREAM_RECORD_BYTES  (CD_STREAM_HEADER_BYTES + CD_STREAM_MAX_PAYLOAD_BYTES)

typedef enum
{
    CD_STREAM_SERVER_ERROR_OPEN   = -1,
    CD_STREAM_SERVER_ERROR_SOCKET = -2
} CdStreamServerError;

/** Playback-local producer counters, copied to the network worker under its mailbox lock. */
typedef struct
{
    unsigned blocks, gaps, max_gap_ms;
    unsigned drive_waits, output_busy, silence_blocks;
    unsigned max_drive_wait_ms, max_output_busy_ms;
} CdStreamSource;

/** Transport state belongs to a single network worker. */
typedef struct
{
    int      fd, subscribed, ending, has_pcm;
    uint32_t next, progress;
    unsigned request_used, pending_size, pending_sent;
    unsigned retrying, stall_id, sampled_at;
    uint32_t retry_since;
    char     request[CD_STREAM_REQUEST_BYTES];
    uint8_t  pending[CD_STREAM_RECORD_BYTES];
} CdStreamClient;

typedef struct CdStreamServer
{
    int listener, discovery;
    int (*nonblocking)(int);
    CdStreamClient clients[CD_STREAM_CLIENTS];
    uint8_t        blocks[CD_STREAM_BLOCKS][CD_STREAM_MAX_PAYLOAD_BYTES];
    uint32_t       write;
    int            finished, opening; /**< Opening remains available until the ring wraps or playback ends. */
    CdStreamSource source;
    unsigned       disconnects, send_retries, resets, mailbox_overflows;
    unsigned       max_lag_blocks, max_poll_gap_ms;
    uint32_t       last_poll;
    const char*    last_disconnect;
    int            last_error;
    unsigned       stall_id, stall_ms, stall_sample_ms, stall_lag, tcp_available;
    Ps2TcpState    stall_tcp;
    Ps2NetTxStats  stall_driver;
} CdStreamServer;

/**
 * @brief Open nonblocking UDP discovery and TCP subscription sockets.
 * @param server Server state to initialize; close it even after a partial failure.
 * @param nonblocking Socket-mode callback; returns zero on success.
 * @return Zero on success, a negative CdStreamServerError on failure.
 */
int cd_stream_server_open(CdStreamServer* server, int (*nonblocking)(int));

/**
 * @brief Service bounded discovery, subscriptions and queued audio sends.
 * @param server Worker-owned server state.
 * @param now Wrapping monotonic milliseconds.
 * @param address Advertised IPv4 address; may be empty while DHCP is pending.
 * @return Zero on success, a negative CdStreamServerError on a listener failure.
 */
int cd_stream_server_step(CdStreamServer* server, uint32_t now, const char* address);

/**
 * @brief Copy one complete resampled sector into the outgoing ring.
 * Slow clients are disconnected by polling rather than blocking the producer.
 * @param server Worker-owned server state.
 * @param pcm CD_STREAM_MAX_PAYLOAD_BYTES of PCM to copy.
 */
void cd_stream_server_push(CdStreamServer* server, const uint8_t* pcm);

/**
 * @brief Cancel subscriptions and discard stale PCM at a playback discontinuity.
 * @param server Worker-owned server state.
 */
void cd_stream_server_reset(CdStreamServer* server);

/**
 * @brief Mark queued PCM for draining followed by an explicit end record.
 * @param server Worker-owned server state; subsequent step calls send and close subscriptions.
 */
void cd_stream_server_end(CdStreamServer* server);

/**
 * @brief Count currently subscribed audio clients.
 * @param server Server state to inspect.
 * @return Number of open clients that completed the subscription handshake.
 */
unsigned cd_stream_server_clients(const CdStreamServer* server);

/**
 * @brief Publish the producer mailbox overflow count to transport diagnostics.
 * @param server Worker-owned server state.
 * @param count Cumulative mailbox overflow count.
 */
void cd_stream_server_overflows(CdStreamServer* server, unsigned count);

/**
 * @brief Close all clients and release discovery and subscription sockets.
 * @param server Initialized or partially opened server state.
 */
void cd_stream_server_close(CdStreamServer* server);
