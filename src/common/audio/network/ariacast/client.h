#pragma once

#include "audio/network/ariacast/websocket.h"
#include "audio/common/metadata_json.h"

#define ARIA_ENDPOINT_AUDIO    1
#define ARIA_ENDPOINT_CONTROL  2
#define ARIA_ENDPOINT_STATS    3
#define ARIA_ENDPOINT_METADATA 4

struct AriaServer;

/**
 * @brief One bounded HTTP/WebSocket connection.
 */
typedef struct
{
    int           fd;                                       /**< Socket, or minus one. */
    int           endpoint;                                 /**< Zero HTTP, one audio, two control, three stats, four metadata. */
    int           closing;                                  /**< Close after flushing the response. */
    unsigned      header_used;                              /**< HTTP header length. */
    unsigned      body_left;                                /**< Metadata body bytes remaining. */
    unsigned      body_used;                                /**< HTTP body bytes in websocket.data before upgrade. */
    unsigned      metadata_revision;                        /**< Last metadata revision sent to this subscriber. */
    uint32_t      audio_opened;                             /**< Audio upgrade time; first-PCM deadline origin. */
    uint32_t      touched;                                  /**< Last received byte time. */
    uint32_t      heartbeat_at;                             /**< Last Ping queued or matching Pong received. */
    uint32_t      ping_token;                               /**< Outstanding Ping payload; zero when idle. */
    uint32_t      peer_address;                             /**< Peer IPv4 address in network byte order. */
    uint32_t      stats_at;                                 /**< Last statistics transmission. */
    char          header[2048];                             /**< Bounded HTTP request header. */
    uint8_t       output[2048 + TRACK_METADATA_JSON_BYTES]; /**< Pending server response bytes. */
    unsigned      output_used;                              /**< Pending byte count. */
    unsigned      output_sent;                              /**< Successfully sent prefix. */
    AriaWebSocket websocket;                                /**< Incremental frame parser. */
} AriaClient;

/**
 * @brief Close a connection and release its audio session if owned.
 * @param server Receiver.
 * @param c Connection.
 * @param reason Disconnect description.
 * @param code Socket error or zero.
 */
void aria_client_close(struct AriaServer* server, AriaClient* c, const char* reason, int code);

/**
 * @brief Close transport while retaining complete audio packets for bounded draining.
 * Non-audio connections close normally; partial WebSocket messages are discarded.
 * @param server Receiver.
 * @param c Connection.
 * @param reason Disconnect description.
 * @param code Socket error or zero.
 * @param now Monotonic milliseconds, starting the drain deadline.
 */
void aria_client_end(struct AriaServer* server, AriaClient* c, const char* reason, int code, uint32_t now);

/**
 * @brief Apply client deadlines, heartbeat and subscription updates.
 * @param server Receiver.
 * @param now Monotonic milliseconds.
 */
void aria_clients_tick(struct AriaServer* server, uint32_t now);

/**
 * @brief Service bounded nonblocking reads and writes for connected clients.
 * @param server Receiver.
 * @param now Monotonic milliseconds.
 */
void aria_clients_service(struct AriaServer* server, uint32_t now);
