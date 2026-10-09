#pragma once

#include "recognition/protocol.h"
#include "audio/artwork/http.h"
#include <stdint.h>

#define CD_LOOKUP_REQUEST_BYTES 1536

enum
{
    LOOKUP_HTTP_OK               = 200,
    LOOKUP_HTTP_ACCEPTED         = 202,
    LOOKUP_HTTP_ERROR            = 400,
    LOOKUP_HTTP_NOT_FOUND        = 404,
    LOOKUP_HTTP_RATE_LIMITED     = 429,
    LOOKUP_HTTP_PROVIDER_FAILURE = 502,
    LOOKUP_HTTP_UNAVAILABLE      = 503
};

/** One worker-owned HTTP exchange. Bodies are heap allocated after bounded headers. */
typedef struct
{
    int            fd;
    int            active;
    int            status;
    int            image;
    int            failed;
    uint32_t       retry_seconds;
    uint32_t       started;
    uint32_t       budget;
    char           header[ARTWORK_HEADER_BYTES + 1];
    unsigned       header_used;
    char           request[CD_LOOKUP_REQUEST_BYTES];
    unsigned       request_size;
    unsigned       sent;
    unsigned       length;
    unsigned       used;
    unsigned char* data;
} CdLookupHttp;

/**
 * @brief Start one bounded nonblocking HTTP exchange on the recognition port.
 * @param http Zero-initialized or previously initialized exchange; any prior exchange is closed.
 * @param host Numeric service IPv4 text for the Host header.
 * @param address Service IPv4 address in host order.
 * @param path Origin-relative API or cover path.
 * @param body JSON request body for POST, or NULL for GET; copied into the request.
 * @param budget Total exchange budget in milliseconds.
 * @param now Wrapping monotonic start time in milliseconds.
 * @return One when started, minus one on setup failure; close the exchange on either outcome.
 */
int cd_lookup_http_open(CdLookupHttp* http, const char* host, uint32_t address, const char* path, const char* body, uint32_t budget, uint32_t now);

/**
 * @brief Advance bounded HTTP sending and receiving within the exchange deadline.
 * The completed body remains owned by the exchange until transferred or closed.
 * @param http Active worker-owned exchange.
 * @param now Wrapping monotonic milliseconds.
 * @return One while pending, zero when its body is complete, minus one on transport or framing failure.
 */
int cd_lookup_http_step(CdLookupHttp* http, uint32_t now);

/**
 * @brief Close an active socket, free its response body and clear exchange state.
 * @param http Zero-initialized or previously initialized exchange.
 */
void cd_lookup_http_close(CdLookupHttp* http);
