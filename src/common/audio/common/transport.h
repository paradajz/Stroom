#pragma once

#define AUDIO_TRANSPORT_MAX_COMMANDS       4
#define AUDIO_TRANSPORT_MAX_PROGRAM_TRACKS 99

/** @brief Source-independent transport operations and playback modes. */
typedef enum
{
    AUDIO_TRANSPORT_PLAY_PAUSE, /**< Toggle playing/paused state; start stopped playback. */
    AUDIO_TRANSPORT_STOP,       /**< Stop playback and reset the current track position. */
    AUDIO_TRANSPORT_PREVIOUS,   /**< Select the preceding track in the current order. */
    AUDIO_TRANSPORT_NEXT,       /**< Select the following track in the current order. */
    AUDIO_TRANSPORT_CONTINUE,   /**< Select sequential playback without repeat. */
    AUDIO_TRANSPORT_SHUFFLE,    /**< Select shuffled playback without repeat. */
    AUDIO_TRANSPORT_REPEAT_ONE, /**< Repeat the current track. */
    AUDIO_TRANSPORT_REPEAT_ALL, /**< Repeat the full sequential track order. */
    AUDIO_TRANSPORT_PROGRAM     /**< Select the stored track program. */
} AudioTransportCommand;

/**
 * @brief Transport requests emitted by one controller update.
 *
 * Commands are ordered; a nonempty program follows them. Scan direction is
 * a held state and must be published each frame, including zero on release.
 * Sources implement only the operations they support; mute belongs to output.
 */
typedef struct
{
    AudioTransportCommand commands[AUDIO_TRANSPORT_MAX_COMMANDS];      /**< Ordered transport and mode commands. */
    unsigned              command_count;                               /**< Valid commands, at most AUDIO_TRANSPORT_MAX_COMMANDS. */
    unsigned              program_count;                               /**< Valid program entries, at most AUDIO_TRANSPORT_MAX_PROGRAM_TRACKS; zero means no submission. */
    int                   program[AUDIO_TRANSPORT_MAX_PROGRAM_TRACKS]; /**< Ordered one-based track numbers. */
    int                   scan_direction;                              /**< Negative rewinds, zero releases, positive advances. */
} AudioTransportRequests;
