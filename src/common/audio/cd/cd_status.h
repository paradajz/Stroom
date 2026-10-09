#pragma once

#include "audio/cd/cd_playback.h"
#include "audio/common/status.h"

/**
 * @brief Published snapshot of disc detection and CD playback.
 */
typedef struct
{
    int            restart_required;                /**< Drive unavailable until console restart. */
    int            present;                         /**< Nonzero when an audio disc is detected. */
    int            tracks;                          /**< Disc track count. */
    int            track;                           /**< One-based current track, or zero before TOC readiness. */
    int            playing;                         /**< Nonzero when playback is running. */
    int            paused;                          /**< Nonzero when playback is paused. */
    int            checking;                        /**< Nonzero while disc detection is pending. */
    int            scanning;                        /**< Scan direction: -1 rewind, 0 idle, 1 forward. */
    unsigned char  programmed[CD_MAX_TRACKS];       /**< Membership of the installed program, indexed by track minus one. */
    unsigned char  played[CD_MAX_TRACKS];           /**< Tracks with audible PCM since program installation. */
    CdPlaybackMode mode;                            /**< Current track-order mode. */
    CdRepeat       repeat;                          /**< Current repeat policy. */
    unsigned       elapsed_seconds;                 /**< Elapsed time in the current track, in seconds. */
    unsigned       duration_seconds;                /**< Current track duration in seconds. */
    unsigned       disc_duration_seconds;           /**< Total playable disc duration in seconds, excluding lead-in. */
    float          track_progress;                  /**< Audible position within the current track, 0..1. */
    float          disc_progress;                   /**< Audible position within the playable disc span, 0..1. */
    unsigned       generation;                      /**< Disc-change generation counter. */
    char           error[AUDIO_STATUS_TEXT_BYTES];  /**< Null-terminated CD error; empty when no failure is reported. */
    char           status[AUDIO_STATUS_TEXT_BYTES]; /**< Null-terminated disc or playback progress message. */
} CdPlaybackStatus;
