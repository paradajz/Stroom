#pragma once

#include <stdint.h>
#include "audio/common/artwork_observations.h"
#include "audio/network/ariacast/protocol.h"

#include "audio/network/ariacast/diagnostics/diagnostic_capture.h"

#define ARIA_DISCONNECT_TEXT_BYTES 48

#if STROOM_DIAGNOSTICS
/** @brief Bounded operation summary for the most recently observed cover. */
typedef struct
{
    unsigned                    count;   /**< Number of operations observed. */
    uint32_t                    maximum; /**< Maximum wall time in milliseconds. */
    AriaDiagnosticCaptureRecord last;    /**< Last operation, retaining its original timestamps and values. */
} AriaCoverTiming;
#endif

/**
 * @brief Worker-owned aggregate counters and retained output-shortage histories.
 */
typedef struct
{
#if STROOM_DIAGNOSTICS
    unsigned              discovery_received;                          /**< Discovery datagrams received. */
    unsigned              discovery_matched;                           /**< Valid discovery queries received. */
    unsigned              discovery_replied;                           /**< Discovery replies sent. */
    unsigned              pings;                                       /**< Heartbeats queued. */
    unsigned              pongs;                                       /**< Matching Pong replies received. */
    unsigned              heartbeat_timeouts;                          /**< Clients closed after an unanswered Ping. */
    uint32_t              last_pong_address;                           /**< IPv4 address of the last matching Pong sender. */
    unsigned              disconnects;                                 /**< Audio connections closed since startup. */
    char                  last_disconnect[ARIA_DISCONNECT_TEXT_BYTES]; /**< Last audio disconnect reason, retained across reconnects. */
    int                   last_disconnect_error;                       /**< Socket errno for the last audio disconnect, or zero. */
    unsigned              last_disconnect_queued;                      /**< PCM frames buffered when audio disconnected. */
    uint32_t              last_disconnect_gap;                         /**< Milliseconds since PCM at the last disconnect. */
    uint32_t              max_poll_gap;                                /**< Largest interval between socket service passes. */
    uint32_t              max_pcm_gap;                                 /**< Largest complete-frame interval within an audio session. */
    unsigned              audio_reads;                                 /**< Audio socket read attempts in this session. */
    unsigned              audio_bytes;                                 /**< Received audio socket bytes in this session. */
    char                  artwork_fetch[256];                          /**< Worker-owned HTTP fetch diagnostic. */
    char                  artwork_display[32];                         /**< Last matching main-thread display stage. */
    uint8_t               metadata_request[ARIA_MESSAGE_BYTES + 1];    /**< Last update: action byte followed by exact request bytes. */
    unsigned              metadata_request_size;                       /**< Diagnostic bytes including the action prefix; zero before an update. */
    AriaDiagnosticCapture diagnostic_capture;                          /**< Retained shortage histories, worker-owned. */
    uint32_t              diagnostic_capture_poll_at;                  /**< Last IOP diagnostic poll time. */
    uint32_t              diagnostic_capture_sample_at;                /**< Last queue sample time. */
    int                   diagnostic_capture_initialized;              /**< Diagnostic service has been selected for this generation. */
    uint32_t              last_bytes_at;                               /**< Time of the last successful audio read in this session. */
    uint32_t              max_byte_gap;                                /**< Longest interval between successful audio reads. */
    unsigned              empty_reads;                                 /**< Audio reads returning a retryable empty result. */
    unsigned              paused_reads;                                /**< Polls intentionally skipped because the PCM queue was full. */
    unsigned              pcm_writes;                                  /**< Successful PCM submissions. */
    unsigned              silence_writes;                              /**< Successful underrun-silence submissions. */
    unsigned              busy_writes;                                 /**< Output submissions deferred for queue space or ownership. */
    unsigned              output_errors;                               /**< Failed sound submissions. */
    unsigned              empty_with_pcm;                              /**< Observations of an empty device queue while PCM was ready. */
    unsigned              empty_without_pcm;                           /**< Observations of an empty device queue with no PCM ready. */
    int                   sound_queued;                                /**< Most recent pre-write device queue size, or minus one when unavailable. */
    uint32_t              last_write_at;                               /**< Most recent successful sound submission in this session. */
    uint32_t              max_write_gap;                               /**< Longest interval between successful sound submissions. */
    uint32_t              max_socket_ms;                               /**< Longest network socket-service pass. */
    uint32_t              max_playback_ms;                             /**< Longest sound-service pass, including preparation and capture. */
    uint32_t              max_artwork_ms;                              /**< Longest artwork-service pass. */
    unsigned              cover_revision;                              /**< Metadata revision of retained cover timings. */
    AriaCoverTiming       cover_phases[DIAGNOSTIC_ARTWORK_PHASE_MAX];  /**< Latest-cover operation summaries. */
#else
    unsigned char unused; /**< Placeholder for the optional diagnostic hook argument. */
#endif
} AriaDiagnostics;

/**
 * @brief Reset session timing while retaining aggregate counters and dropout evidence.
 * @param d Diagnostic state.
 */
void aria_diagnostics_session(AriaDiagnostics* d);

/**
 * @brief Record the sound queue observation and submission outcome without extra RPC.
 * @param d Diagnostic state.
 * @param now Monotonic time of submission attempt.
 * @param pcm Nonzero when PCM was ready before the attempt.
 * @param queued Pre-write sound queue size, or minus one when unavailable.
 * @param result Submission result: zero accepted, positive busy, negative failed.
 */
void aria_diagnostics_output(AriaDiagnostics* d, uint32_t now, int pcm, int queued, int result);

#if STROOM_DIAGNOSTICS
/** Retain timings even when no output-shortage event is triggered. Worker only. */
void aria_diagnostics_cover(AriaDiagnostics* d, AriaDiagnosticCaptureRecord record);
#endif
