#pragma once

#include <stdint.h>
#include "contracts/metadata.h"
#include "audio/network/ariacast/protocol.h"

/** Failure codes for this API. */
typedef enum
{
    ARIA_STREAM_ERROR_ENDING     = -1,
    ARIA_STREAM_ERROR_PCM_SIZE   = -2,
    ARIA_STREAM_ERROR_QUEUE_FULL = -3,
} AriaStreamError;

#define ARIA_QUEUE_MESSAGES     25
#define ARIA_PREBUFFER_MESSAGES 13
#define ARIA_IDLE_MS            3000

/**
 * @brief Worker-owned PCM queue and session lifetime, independent of sockets.
 *
 * Queue entries are complete PCM messages, each containing ARIA_PCM_FRAMES
 * stereo sample frames. Generation starts at zero on server open and increments
 * on each audio WebSocket establishment and session release, wrapping as unsigned.
 * Compare generations for inequality to detect a change; they are not a session
 * count and are not unique across server reopenings.
 */
typedef struct
{
    uint8_t  pcm[ARIA_QUEUE_MESSAGES][ARIA_PCM_BYTES];     /**< Complete PCM messages awaiting sound output. */
    char     device_name[METADATA_DEVICE_NAME_BYTES];      /**< Sender label negotiated before PCM; empty when unnamed. */
    int      ending;                                       /**< Transport ended; complete queued PCM remains playable. */
    int      finished;                                     /**< Queue and sound lead-out completed; session may be released. */
    uint32_t ending_at;                                    /**< Transport end time; bounds draining even if output stalls. */
    int      listening;                                    /**< Session negotiated analysis-only PCM consumption before its first packet. */
    unsigned read;                                         /**< Index of the oldest queued PCM message. */
    unsigned count;                                        /**< Queued messages. */
    uint32_t last_pcm;                                     /**< Last complete PCM receipt time. */
    unsigned received;                                     /**< Complete PCM messages accepted since the latest audio connection. */
    unsigned generation;                                   /**< Change token incremented on audio connection establishment and session release. */
    int      has_pcm;                                      /**< Nonzero after the first PCM message, independent of counter wrap. */
    int      connected;                                    /**< Nonzero while the transport owns an audio connection. */
    void (*disconnect)(void* context, const char* reason); /**< Transport callback for output failure. */
    void* context;                                         /**< Disconnect callback context. */
} AriaStream;

/**
 * @brief Test whether PCM is active or a disconnected session is within its drain deadline.
 * @param stream PCM session.
 * @param now Monotonic milliseconds.
 * @return Nonzero while audio is active.
 */
int aria_stream_active(const AriaStream* stream, uint32_t now);

/**
 * @brief Queue one complete PCM message without discarding older audio.
 * @param stream PCM session.
 * @param data Interleaved stereo PCM16 message.
 * @param size Message bytes.
 * @param now Receipt time in milliseconds.
 * @return 0 on success, a negative AriaStreamError on failure.
 */
int aria_stream_push(AriaStream* stream, const uint8_t* data, unsigned size, uint32_t now);

/**
 * @brief Consume the oldest message after successful sound submission.
 * @param stream PCM session.
 */
void aria_stream_consume(AriaStream* stream);
