#pragma once

#include "audio/common/snapshot.h"
#include "contracts/metadata.h"
#include "audio/cd/cd_status.h"
#include "audio/common/status.h"
#include "audio/common/metadata.h"
#include "audio/common/transport.h"

/** Failure codes for this API. */
typedef enum
{
    AUDIO_SOURCE_ERROR_CD_CLOSE                = -1,
    AUDIO_SOURCE_ERROR_NETWORK_CLOSE           = -2,
    AUDIO_SOURCE_ERROR_OUTPUT_CLOSE            = -3,
    AUDIO_SOURCE_ERROR_OUTPUT_OPEN             = -4,
    AUDIO_SOURCE_ERROR_OUTPUT_RESTART_REQUIRED = -5,
} AudioSourceError;

typedef enum
{
    AUDIO_SOURCE_NETWORK,
    AUDIO_SOURCE_CD,
    AUDIO_SOURCE_CHECKING,
    AUDIO_SOURCE_WAITING,
    AUDIO_SOURCE_ERROR
} AudioSourceKind;

/**
 * @brief Published source selection, network readiness, and CD state.
 */
typedef struct
{
    char             device_name[METADATA_DEVICE_NAME_BYTES]; /**< Sender-provided input label; empty when unnamed. */
    int              listening;                               /**< Network session disables PS2 sound output. */
    AudioSourceKind  kind;                                    /**< Currently selected source, pending detection/cleanup, or startup/cleanup error. */
    unsigned         network_generation;                      /**< Stream generation token captured with activity; compare for inequality. */
    int              network_ready;                           /**< Nonzero while networking is initialized and has no runtime failure. */
    int              network_waiting;                         /**< Nonzero when network audio is selected but inactive. */
    char             network_address[16];                     /**< Null-terminated console IPv4 address; empty if unavailable. */
    char             error[AUDIO_STATUS_TEXT_BYTES];          /**< Null-terminated source failure or cleanup progress text. */
    TrackMetadata    metadata;                                /**< Current track labels and artwork; empty when unavailable. */
    CdPlaybackStatus cd;                                      /**< Most recent CD worker snapshot. */
} AudioSourceStatus;

/**
 * @brief Start source detection and initialize playback preferences.
 *
 * Reopening first closes previous sources; pending cleanup leaves their state intact.
 * Failure to create shared output synchronization disables audio until console restart.
 * The application can still load its UI and poll the retained error.
 *
 * @param autoplay Start newly inserted CDs automatically when nonzero.
 * @param muted Start audio output muted when nonzero.
 * @return 0 when shared output opens and detection is started or disabled by a
 * previous CD failure, positive while previous cleanup is pending, a negative
 * AudioSourceError on failure. CD and network initialization happen asynchronously.
 */
int audio_source_open(int autoplay, int muted);

/**
 * @brief Select CDs at startup and while waiting for network audio.
 *
 * Active network streams defer disc detection until they disconnect.
 * Pending worker cleanup shows a neutral waiting state and is retried before
 * source selection resumes. After five seconds of unfinished cleanup, the waiting
 * screen asks for console restart; resources remain owned and cleanup stays retryable.
 * Cleanup failures identify the affected source;
 * startup/runtime failures retain their cause throughout cleanup and retry.
 * Failed CD startup leaves network playback available once cleanup succeeds;
 * CD retries are delayed and deferred while a network stream is active.
 * Failed network startup reports an error until cleanup and a later retry succeed.
 * Incompatible resident network modules disable startup retries until application
 * restart (a console restart replaces those modules), retaining the explanation
 * while CD detection and playback continue. Cleanup remains retryable.
 * Fatal network runtime errors use the same cleanup and delayed restart path.
 * @param audio Destination audio snapshot.
 * @param status Destination source status.
 */
void audio_source_poll(Audio* audio, AudioSourceStatus* status);

/**
 * @brief Request mute for shared sound output without changing transport.
 *
 * Call on the main thread after opening sources. Workers apply the preference
 * without pausing playback or audio analysis, across source changes.
 *
 * @param muted Nonzero to mute audible output; zero to restore sound.
 */
void audio_source_set_muted(int muted);

/**
 * @brief Dispatch shared transport requests to the source in a polled snapshot.
 *
 * Call on the main thread after updating controllers. CD requests use the
 * snapshot's disc generation so stale requests cannot affect a replacement.
 * Network transport is currently sender-controlled; network and detecting
 * snapshots accept no local transport requests. Output mute is independent.
 *
 * @param status Non-NULL source snapshot used for this controller update.
 * @param requests Non-NULL requests with valid counts; borrowed for this call
 * and left unchanged. Backend submission copies their contents.
 */
void audio_source_apply(const AudioSourceStatus* status, const AudioTransportRequests* requests);

/**
 * @brief Stop both audio source workers and release their resources.
 * @return 0 after cleanup, positive while stopping, a negative AudioSourceError on failure.
 * Nonzero results retain ownership for a later close attempt.
 */
int audio_source_close(void);
