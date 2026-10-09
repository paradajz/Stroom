#pragma once

#include <stddef.h>
#include <stdint.h>
#include "audio/artwork/artwork.h"

/** Failure codes for this API. */
typedef enum
{
    ARTWORK_HTTP_ERROR_URL        = -1,
    ARTWORK_HTTP_ERROR_ADDRESS    = -2,
    ARTWORK_HTTP_ERROR_PORT       = -3,
    ARTWORK_HTTP_ERROR_PATH       = -4,
    ARTWORK_HTTP_ERROR_RESPONSE   = -5,
    ARTWORK_HTTP_ERROR_TOO_LARGE  = -6,
    ARTWORK_HTTP_ERROR_EMPTY_BODY = -7,
    ARTWORK_HTTP_ERROR_INCOMPLETE = -8,
} ArtworkHttpError;

#define ARTWORK_HEADER_BYTES 2048

/**
 * @brief Parsed numeric-IPv4 HTTP artwork destination.
 */
typedef struct
{
    char     host[16];                         /**< Numeric IPv4 host; DNS and TLS are not used. */
    uint32_t address;                          /**< IPv4 address in host byte order, independent of SDK inet hooks. */
    unsigned port;                             /**< TCP port, default 80. */
    char     path[METADATA_ARTWORK_URL_BYTES]; /**< Request target including query string. */
} ArtworkUrl;

/**
 * @brief Incremental bounded HTTP response framing, independent of sockets.
 */
typedef struct
{
    char     header[ARTWORK_HEADER_BYTES]; /**< Header or chunk-size line under construction. */
    unsigned header_used;                  /**< Collected header/line bytes. */
    unsigned remaining;                    /**< Body or current chunk bytes remaining. */
    unsigned used;                         /**< Compressed image bytes collected. */
    int      status_code;                  /**< HTTP response status, zero before headers arrive. */
    int      state;                        /**< Internal framing phase; a negative ArtworkHttpError on failure. */
    int      chunked;                      /**< Nonzero for chunked transfer encoding. */
    int      too_large;                    /**< Response exceeds the compressed-image byte limit. */
    int      done;                         /**< Nonzero after a complete response. */
} ArtworkHttp;

/**
 * @brief Parse an HTTP URL without DNS or credentials.
 * @param url Complete URL.
 * @param parsed Destination on success.
 * @return 0 on success, a negative ArtworkHttpError on failure.
 */
int artwork_url_parse(const char* url, ArtworkUrl* parsed);

/**
 * @brief Consume response bytes, decoding chunk framing when present.
 * @param http Zero-initialized parser state.
 * @param data Received bytes.
 * @param size Received byte count.
 * @param image Destination with capacity for min(used + size, ARTWORK_MAX_BYTES) bytes.
 * @return 0 for a complete nonempty body, positive while incomplete, negative on malformed input.
 */
int artwork_http_feed(ArtworkHttp* http, const void* data, size_t size, uint8_t* image);

/**
 * @brief Finish a connection-delimited response on orderly socket closure.
 * @param http Parser state.
 * @return 0 for a complete nonempty body, negative for invalid or truncated input.
 */
int artwork_http_eof(ArtworkHttp* http);
