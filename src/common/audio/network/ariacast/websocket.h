#pragma once

#include <stddef.h>
#include <stdint.h>

#include "audio/network/ariacast/protocol.h"

/** Failure codes for this API. */
typedef enum
{
    ARIA_WEBSOCKET_ERROR_KEY              = -1,
    ARIA_WEBSOCKET_ERROR_LENGTH           = -2,
    ARIA_WEBSOCKET_ERROR_HEADER           = -3,
    ARIA_WEBSOCKET_ERROR_MESSAGE_REJECTED = -4,
} AriaWebSocketError;

#define ARIA_WS_ACCEPT_BYTES  29
#define ARIA_WS_HEADER_BYTES  8
#define ARIA_WS_CONTROL_BYTES 125
#define ARIA_WS_TEXT          1
#define ARIA_WS_BINARY        2
#define ARIA_WS_CLOSE         8
#define ARIA_WS_PING          9
#define ARIA_WS_PONG          10

/**
 * @brief Incremental masked-client WebSocket decoder with bounded message storage.
 */
typedef struct
{
    uint8_t  header[ARIA_WS_HEADER_BYTES];   /**< Current frame header. */
    unsigned header_used;                    /**< Collected header bytes. */
    unsigned header_size;                    /**< Required header bytes. */
    unsigned remaining;                      /**< Remaining frame payload. */
    unsigned offset;                         /**< Frame payload offset for masking. */
    unsigned used;                           /**< Reassembled data message length. */
    unsigned control_used;                   /**< Collected control payload length. */
    uint8_t  mask[4];                        /**< Client masking key. */
    uint8_t  data[ARIA_MESSAGE_BYTES];       /**< Reassembled text/binary message. */
    uint8_t  control[ARIA_WS_CONTROL_BYTES]; /**< Ping, pong or close payload. */
    unsigned opcode;                         /**< Current frame opcode. */
    unsigned message_opcode;                 /**< Fragmented message opcode, or zero. */
    int      closed;                         /**< Close received; ignore further input. */
    int      final;                          /**< Current FIN bit. */
} AriaWebSocket;

/**
 * @brief Receive one decoded message.
 * @param context Caller state.
 * @param opcode Message type.
 * @param data Payload.
 * @param size Payload length.
 * @return Nonzero to continue.
 */
typedef int (*AriaMessage)(void* context, unsigned opcode, const uint8_t* data, unsigned size);

/**
 * @brief Derive the RFC 6455 accept value from a validated client key.
 * @param key Base64 client nonce.
 * @param accept Destination of 29 bytes.
 * @return 0 on success, a negative AriaWebSocketError on failure.
 */
int aria_websocket_accept(const char* key, char accept[ARIA_WS_ACCEPT_BYTES]);

/**
 * @brief Decode arbitrary portions of a masked WebSocket byte stream.
 * A Close is delivered once; remaining and subsequent input is ignored.
 * @param ws Decoder.
 * @param data Input bytes.
 * @param size Input length.
 * @param message Message callback.
 * @param context Callback state.
 * @return 0 on success, a negative AriaWebSocketError on failure.
 */
int aria_websocket_feed(AriaWebSocket* ws, const uint8_t* data, size_t size, AriaMessage message, void* context);

/**
 * @brief Encode an unmasked server frame.
 * @param output Destination.
 * @param capacity Destination bytes.
 * @param opcode Frame type.
 * @param data Payload.
 * @param size Payload bytes.
 * @return Encoded byte count, or zero if too large.
 */
unsigned aria_websocket_frame(uint8_t* output, unsigned capacity, unsigned opcode, const void* data, unsigned size);
