#pragma once

#include "contracts/cd_stream.h"
#include "platform/network/tcp_state_wire.h"
#include "platform/network/rpc/driver_stats.h"
#include <stdint.h>

#define CD_STREAM_CLIENTS       4
#define CD_STREAM_BLOCKS        128
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

/** Open UDP discovery and TCP subscription sockets. Close after failure. */
int cd_stream_server_open(CdStreamServer* server, int (*nonblocking)(int));

/** Poll bounded nonblocking work; address may be empty until DHCP completes. */
int cd_stream_server_step(CdStreamServer* server, uint32_t now, const char* address);

/** Copy a complete resampled sector. Slow clients never stall CD playback. */
void cd_stream_server_push(CdStreamServer* server, const uint8_t* pcm);

/** Cancel subscriptions and stale PCM on a playback discontinuity. */
void cd_stream_server_reset(CdStreamServer* server);

/** Send queued PCM and an explicit end before closing subscriptions. */
void     cd_stream_server_end(CdStreamServer* server);
unsigned cd_stream_server_clients(const CdStreamServer* server);

/** Publish mailbox overflows observed by the network worker. */
void cd_stream_server_overflows(CdStreamServer* server, unsigned count);
void cd_stream_server_close(CdStreamServer* server);
