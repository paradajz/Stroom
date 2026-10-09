#pragma once

#include "contracts/diagnostic.h"
#include <stdint.h>

/** Failure codes for this API. */
typedef enum
{
    PS2_DIAGNOSTIC_ERROR_SOCKET = -1,
    PS2_DIAGNOSTIC_ERROR_RPC    = -2,
    PS2_DIAGNOSTIC_ERROR_ABI    = -3,
} Ps2DiagnosticError;

#define DIAGNOSTIC_IOP_RPC        129
#define DIAGNOSTIC_IOP_EXPORT     58
#define DIAGNOSTIC_IOP_ABI        0x54524331u
#define DIAGNOSTIC_IOP_BATCH      14
#define DIAGNOSTIC_IOP_RING       256
#define DIAGNOSTIC_IOP_SELECT     1
#define DIAGNOSTIC_IOP_POLL       2
#define DIAGNOSTIC_IOP_READ_BEGIN 3
#define DIAGNOSTIC_IOP_READ_END   4

/**
 * @brief One IOP observation with event-specific fields, all host-endian words.
 */
typedef struct
{
    uint32_t at;                              /**< IOP monotonic milliseconds. */
    uint32_t kind;                            /**< PS2_DIAGNOSTIC event code. */
    uint32_t data[DIAGNOSTIC_IOP_DATA_WORDS]; /**< Event-specific values documented in the diagnostic guide. */
} Ps2DiagnosticRecord;

/**
 * @brief Versioned request to the optional SDK diagnostic export.
 */
typedef struct
{
    uint32_t abi;     /**< Must equal DIAGNOSTIC_IOP_ABI. */
    uint32_t command; /**< Select, poll, or record a socket read. */
    int32_t  socket;  /**< IOP socket descriptor, or minus one to disable. */
    int32_t  result;  /**< Socket read result for DIAGNOSTIC_IOP_READ_END. */
} Ps2DiagnosticRequest;

/**
 * @brief Bounded RPC response, smaller than the EE client's 512-byte buffer.
 */
typedef struct
{
    int32_t             status;                        /**< One if supported, zero if absent, negative on error. */
    uint32_t            abi;                           /**< Reply ABI marker. */
    uint32_t            now;                           /**< IOP time sampled before draining records. */
    uint32_t            lost;                          /**< Cumulative overwritten IOP records since selection. */
    uint32_t            count;                         /**< Number of returned records. */
    uint32_t            peer;                          /**< Selected peer IPv4 in host byte order. */
    uint32_t            port;                          /**< Selected peer port in host byte order. */
    Ps2DiagnosticRecord records[DIAGNOSTIC_IOP_BATCH]; /**< Oldest-first observations. */
} Ps2DiagnosticReply;

_Static_assert(sizeof(Ps2DiagnosticReply) <= 512, "Diagnostic reply must fit the EE RPC buffer");

#if defined(STROOM_DIAGNOSTICS) && STROOM_DIAGNOSTICS
/**
 * @brief Invoke the private bridge's optional network diagnostic service.
 * @param request Request copied before any reply overwrites the RPC buffer.
 * @param reply Destination with availability and records.
 */
void platform_net_diagnostic(const Ps2DiagnosticRequest* request, Ps2DiagnosticReply* reply);

/**
 * @brief Record an IOP socket read boundary through the optional SDK export.
 * @param socket Socket descriptor.
 * @param result Actual read return value for a completed read.
 * @param complete Nonzero after read, zero before read.
 */
void platform_net_diagnostic_read(int socket, int result, int complete);
#endif
