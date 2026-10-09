#pragma once

#include "contracts/diagnostic.h"
#include "util/diagnostics.h"
#include <stddef.h>
#include <stdint.h>

#define DIAGNOSTIC_CAPTURE_STAGE_LOCK  5
#define DIAGNOSTIC_CAPTURE_STAGE_SLEEP 6
#define DIAGNOSTIC_CAPTURE_SLOW_MS     5

#define DIAGNOSTIC_CAPTURE_EVENTS        3
#define DIAGNOSTIC_CAPTURE_BEFORE_MS     5000
#define DIAGNOSTIC_CAPTURE_AFTER_MS      2000
#define DIAGNOSTIC_CAPTURE_OUTPUT_GAP_MS 100

/**
 * @brief One timestamped observation; data meaning is defined by kind.
 */
typedef struct
{
    uint32_t at;                                  /**< EE monotonic milliseconds, estimated for IOP observations. */
    uint32_t kind;                                /**< Event code documented in the network diagnostic guide. */
    uint32_t data[DIAGNOSTIC_CAPTURE_DATA_WORDS]; /**< Fixed-width event-specific values. */
} AriaDiagnosticCaptureRecord;

/**
 * @brief Immutable completed event or a currently collecting event.
 */
typedef struct
{
    uint32_t                    id;                                  /**< Nonzero event identifier, not reused by clear. */
    uint32_t                    session;                             /**< Audio generation when triggered. */
    unsigned                    reason;                              /**< One for inserted silence, two for a long accepted-output gap. */
    unsigned                    history_limited;                     /**< Rolling capacity shortened the requested prehistory. */
    uint32_t                    trigger;                             /**< Silence submission or resumed output after a long gap. */
    unsigned                    count;                               /**< Number of saved records. */
    unsigned                    truncated;                           /**< Records omitted because the event filled. */
    int                         complete;                            /**< Post-trigger window has ended. */
    uint32_t                    peer;                                /**< Audio peer IPv4 in host byte order. */
    uint32_t                    port;                                /**< Audio peer TCP port. */
    uint32_t                    byte_base;                           /**< EE byte count when IOP diagnostic recording began. */
    AriaDiagnosticCaptureRecord records[DIAGNOSTIC_CAPTURE_RECORDS]; /**< Preceding and following observations. */
} AriaDiagnosticCaptureEvent;

/**
 * @brief Worker-owned bounded recorder; completed slots require explicit clearing.
 */
typedef struct
{
#if STROOM_DIAGNOSTICS
    AriaDiagnosticCaptureRecord rolling[DIAGNOSTIC_CAPTURE_RECORDS]; /**< Recent observations. */
    AriaDiagnosticCaptureEvent  events[DIAGNOSTIC_CAPTURE_EVENTS];   /**< Retained shortage events. */
#endif
    unsigned write;           /**< Next rolling slot. */
    unsigned count;           /**< Valid rolling records. */
    unsigned missed;          /**< Triggers not retained because all slots were occupied. */
    uint32_t next_id;         /**< Last assigned identifier. */
    uint32_t session;         /**< Current audio generation. */
    int      iop_status;      /**< Most recent optional diagnostic service status. */
    uint32_t iop_records;     /**< Number of actual IOP observations received in this session. */
    unsigned sack_known;      /**< A SACK state observation arrived for this session. */
    unsigned sack_enabled;    /**< Resident stack compiled with SACK output support. */
    unsigned sack_negotiated; /**< Selected PCB retained negotiated SACK support. */
    uint32_t iop_lost;        /**< Latest count of overwritten IOP observations. */
    uint32_t peer;            /**< Current audio peer address. */
    uint32_t port;            /**< Current audio peer port. */
    uint32_t byte_base;       /**< Current IOP read counter origin in EE bytes. */
    uint32_t last_output_at;  /**< Time of the previous accepted sound write. */
    int      had_output;      /**< An accepted write exists in this session. */
    int      in_shortage;     /**< Silence has been submitted without subsequent PCM. */
} AriaDiagnosticCapture;

#if STROOM_DIAGNOSTICS
/**
 * @brief Record an observation and append it to any collecting event.
 * @param f Recorder.
 * @param record Observation with an EE timestamp.
 */
void aria_diagnostic_capture_record(AriaDiagnosticCapture* f, AriaDiagnosticCaptureRecord record);

/**
 * @brief Capture inserted silence or a long output gap; coalesce triggers within a capture.
 * @param f Recorder.
 * @param now Current EE time.
 * @param session Audio generation.
 * @param pcm Nonzero for accepted PCM, zero for accepted silence.
 */
void aria_diagnostic_capture_output(AriaDiagnosticCapture* f, uint32_t now, uint32_t session, int pcm);

/**
 * @brief End expired capture windows even when no new audio arrives.
 * @param f Recorder.
 * @param now Current EE time.
 */
void aria_diagnostic_capture_tick(AriaDiagnosticCapture* f, uint32_t now);

/**
 * @brief Start a new session without losing saved events.
 * @param f Recorder.
 * @param session New audio generation.
 */
void aria_diagnostic_capture_session(AriaDiagnosticCapture* f, uint32_t session);

/**
 * @brief Handle a bounded diagnostic manifest, page, or explicit clear request.
 * @param f Recorder.
 * @param query NUL-terminated UDP command.
 * @param reply JSON destination.
 * @param capacity Destination bytes.
 * @return One for a recognized command, otherwise zero.
 */
int aria_diagnostic_capture_reply(AriaDiagnosticCapture* f, const char* query, char* reply, size_t capacity);
#endif
