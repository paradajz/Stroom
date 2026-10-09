#pragma once

#include "audio/network/ariacast/client.h"
#include "audio/network/ariacast/stream.h"
#include "audio/network/ariacast/diagnostics/diagnostics.h"
#include "audio/common/status.h"
#include "audio/common/metadata_json.h"

/** Failure codes for this API. */
typedef enum
{
    ARIA_SERVER_ERROR_OPEN = -1,
} AriaServerError;

#define ARIA_CLIENTS              6
#define ARIA_FIRST_PCM_TIMEOUT_MS 30000

#if STROOM_DIAGNOSTICS
#define ARIA_DIAGNOSTICS(server) (&(server)->diagnostics)
#else
#define ARIA_DIAGNOSTICS(server) NULL
#endif

/**
 * @brief Worker-owned AriaCast receiver, discovery and bounded PCM jitter queue.
 */
typedef struct AriaServer
{
    char error[AUDIO_STATUS_TEXT_BYTES]; /**< Failed operation, port and error code; retained after cleanup. */
    const char* volatile operation;      /**< Current socket operation, for worker-stall diagnostics. */
#if STROOM_DIAGNOSTICS
    AriaDiagnostics diagnostics; /**< Optional inspection state, absent from release. */
#endif
    AriaStream    stream;                /**< PCM session consumed by playback. */
    int           listener;              /**< TCP listener socket. */
    int           discovery;             /**< UDP discovery socket. */
    int           audio_fd;              /**< Sole audio sender, or minus one. */
    AriaClient    clients[ARIA_CLIENTS]; /**< Fixed connection slots. */
    uint32_t      last_discovery_poll;   /**< Last discovery and new-connection check. */
    uint32_t      last_poll;             /**< Last bounded socket-service pass. */
    uint32_t      heartbeat_serial;      /**< Unique Ping token within this receiver lifetime. */
    TrackMetadata metadata;              /**< Worker-owned current sender labels. */
    uint32_t      metadata_peer;         /**< IPv4 owner of current labels; zero when empty. */
    uint32_t      metadata_at;           /**< Last accepted metadata update time. */
    unsigned      metadata_revision;     /**< Changes observed by WebSocket subscribers. */
    int           permitted;             /**< Nonzero when CD does not own input. */
    int (*nonblocking)(int fd);          /**< Socket configuration callback. */
} AriaServer;

/**
 * @brief Open discovery and streaming sockets.
 * @param server Zeroable receiver state.
 * @param nonblocking Set a socket nonblocking; 0 on success, a negative AriaServerError on failure.
 * @return 0 on success, a negative AriaServerError on failure.
 */
int aria_server_open(AriaServer* server, int (*nonblocking)(int fd));

/**
 * @brief Service bounded socket work without blocking.
 * @param server Receiver.
 * @param now Monotonic milliseconds.
 * @param address Advertised IPv4 address.
 * @param permitted Whether a new audio source may use this receiver.
 */
void aria_server_step(AriaServer* server, uint32_t now, const char* address, int permitted);

/**
 * @brief Check for recent complete audio messages.
 * @param server Receiver.
 * @param now Monotonic milliseconds.
 * @return Nonzero while sender audio is live.
 */
int aria_server_active(const AriaServer* server, uint32_t now);

/**
 * @brief Disconnect the sender and retain the reason before clearing buffered sound.
 * @param server Receiver.
 * @param reason Diagnostic description for the disconnect.
 */
void aria_server_disconnect(AriaServer* server, const char* reason);

/**
 * @brief Close all receiver sockets.
 * @param server Receiver.
 */
void aria_server_close(AriaServer* server);

/**
 * @brief Format protocol statistics and optional diagnostic extensions.
 * @param server Receiver.
 * @param stats JSON destination.
 * @param capacity Destination size.
 */
void aria_statistics(const AriaServer* server, char* stats, size_t capacity);
