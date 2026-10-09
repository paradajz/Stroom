#pragma once

#include "platform/cd/drive.h"
#include "audio/cd/cd_output.h"
#include "audio/cd/cd_scan.h"
#include "audio/cd/cd_transport.h"

/**
 * @brief One worker iteration's requests copied from the shared command mailbox.
 */
typedef struct
{
    unsigned              generation;             /**< Disc insertion this request batch belongs to. */
    int                   have_command;           /**< Nonzero when command is valid. */
    AudioTransportCommand command;                /**< Oldest queued transport command. */
    int                   scan_direction;         /**< Latest held scan direction. */
    int                   program[CD_MAX_TRACKS]; /**< Requested program track order. */
    unsigned              program_count;          /**< Number of requested program entries, or zero. */
} CdRequests;

/**
 * @brief Playback decisions and component state owned exclusively by the CD worker.
 */
typedef struct
{
    int              autoplay;
    CdPlaybackStatus status;         /**< Current disc/playback snapshot. */
    CdToc            toc;            /**< Validated track boundaries. */
    CdPlayback       playback;       /**< Track order and repeat policy. */
    CdScan           scan;           /**< Held scan timing. */
    int              scan_inhibited; /**< Ignore held scanning after Stop until a release arrives. */
    Ps2CdDrive       drive;          /**< Sector read buffers and pending request state. */
    CdOutput         output;         /**< Sound readiness and resampling history. */
    const CdRuntime* runtime;        /**< Worker callbacks, valid until close. */
    int              drive_ready;    /**< Nonzero after drive services initialize. */
    int              position;       /**< Estimated audible sector or requested seek position. */
    int              submitted;      /**< Sector immediately after the last submitted audio. */
    int              tail_blocks;    /**< Silence blocks submitted at the current playback boundary. */
    int              restart;        /**< Nonzero when next step must reset streaming at position. */
    unsigned         generation;     /**< Monotonic disc insertion count. */
    unsigned         toc_attempts;   /**< Attempts to read the current disc's track table. */
    uint32_t         toc_started;    /**< First track-table attempt in the current retry window. */
    uint32_t         checked;        /**< Last media probe timestamp. */
} CdController;

/**
 * @brief Initialize playback state and drive services on the CD worker.
 * @param c Controller to initialize.
 * @param runtime Worker callbacks, retained until close.
 * @param now Monotonic milliseconds.
 * @param autoplay Start newly inserted discs automatically when nonzero.
 */
void cd_controller_open(CdController* c, const CdRuntime* runtime, uint32_t now, int autoplay);

/**
 * @brief Check media and initialize newly detected discs before accepting requests.
 * @param c Controller state.
 * @param now Monotonic milliseconds.
 */
void cd_controller_detect(CdController* c, uint32_t now);

/**
 * @brief Apply requests, service streaming, and update playback status.
 * @param c Controller state.
 * @param requests Commands copied after media detection.
 * @param now Monotonic milliseconds.
 */
void cd_controller_step(CdController* c, const CdRequests* requests, uint32_t now);

/**
 * @brief Cancel outstanding reads and release sound output.
 * @param c Controller state.
 */
void cd_controller_close(CdController* c);
