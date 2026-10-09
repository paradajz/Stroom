#pragma once

#include "milkdrop/preset.h"

/** Failure codes for this API. */
typedef enum
{
    DIRECTOR_ERROR_FRAME_RATE          = -1,
    DIRECTOR_ERROR_NO_ELIGIBLE_PRESETS = -2,
} DirectorError;

#define DIRECTOR_MIN_SECONDS 5
#define DIRECTOR_MAX_SECONDS 300

typedef enum
{
    DIRECTOR_FIXED,
    DIRECTOR_SEQUENTIAL,
    DIRECTOR_SHUFFLE,
    DIRECTOR_MODE_COUNT
} DirectorMode;

/**
 * @brief Preset scheduler, shared clock, and active transition state.
 */
typedef struct
{
    Preset       current;                  /**< Current preset instance. */
    Preset       next;                     /**< Pending transition destination. */
    uint32_t     random;                   /**< Mutable preset-selection RNG state. */
    DirectorMode mode;                     /**< Automatic selection policy. */
    PresetKind   order[MILK_PRESET_COUNT]; /**< Shuffled automatic playback order, sized to the compiled library. */
    unsigned     order_count;              /**< Number of eligible presets. */
    int          frame_rate;               /**< Minimum measured FPS; zero disables filtering for benchmarks. */
    unsigned     order_at;                 /**< Index of the next preset in order; order_count means exhausted. */
    int          transitioning;            /**< Nonzero during a preset transition. */
    float        elapsed;                  /**< Elapsed preset interval in seconds. */
    float        duration;                 /**< Base preset interval in seconds. */
    float        blend_time;               /**< Elapsed blend time in seconds. */
    float        blend_duration;           /**< Target blend duration in seconds. */
    unsigned     variation;                /**< Maximum additional interval delay, 0..10 seconds. */
    float        interval_extra;           /**< Additional delay sampled once for the current interval, in seconds. */
    float        time;                     /**< Shared visualization time in seconds. */
    unsigned     frame;                    /**< Shared visualization frame number. */
    int          hard_cuts;                /**< Nonzero to enable audio-triggered hard cuts. */
    float        hard_cut_threshold;       /**< Adaptive threshold applied to relative band energy. */
} Director;

/**
 * @brief Shuffle the library and choose its first preset with a 10-second interval and up to 3 seconds variation.
 *
 * @param d Director to initialize.
 * @param seed Random seed; zero selects a built-in seed.
 */
void director_init(Director* d, uint32_t seed);

/**
 * @brief Advance presets and scheduled transitions using the shared audio clock.
 *
 * @param d Director to update.
 * @param audio Current features and shared time.
 * @param dt Actual visible elapsed seconds; nonpositive or non-finite values are ignored.
 */
void director_step(Director* d, const MusicFeatures* audio, float dt);

/**
 * @brief Start the next automatic transition unless one is already running.
 *
 * @param d Director whose mode chooses sequential or shuffled selection.
 */
void director_next(Director* d);

/** Apply the 30/60 FPS eligibility filter, canceling blends and replacing an
 * ineligible current preset immediately. Returns 0 on success, a negative
 * DirectorError for an invalid filter or when no preset qualifies. */
int director_set_frame_rate(Director* d, int frame_rate);

/**
 * @brief Browse library order, advancing from any pending transition destination.
 * In shuffle mode, start a fresh shuffled cycle from the chosen preset.
 *
 * @param d Director to update.
 * @param direction Negative selects previous, positive next, zero does nothing.
 */
void director_move(Director* d, int direction);

/**
 * @brief Change scheduling mode and restart the interval; fixed mode cancels blending.
 * Entering shuffle mode starts a shuffled cycle from the current or pending preset.
 *
 * @param d Director to update.
 * @param mode New scheduling mode; invalid values are ignored.
 */
void director_set_mode(Director* d, DirectorMode mode);

/**
 * @brief Adjust the interval by five seconds within 5..300 and restart its timer.
 *
 * @param d Director to update.
 * @param direction Negative decreases, positive increases, zero does nothing.
 */
void director_adjust_duration(Director* d, int direction);

/**
 * @brief Adjust maximum interval variation by one second within 0..10 and resample it.
 *
 * @param d Director to update.
 * @param direction Negative decreases, positive increases, zero does nothing.
 */
void director_adjust_variation(Director* d, int direction);

/**
 * @brief Get the display label for a scheduling mode.
 *
 * @param mode Mode to describe.
 * @return Static uppercase label; invalid modes use FIXED.
 */
const char* director_mode_name(DirectorMode mode);

/**
 * @brief Read linear transition progress.
 *
 * @param d Director to inspect.
 * @return Progress in 0..1, or zero outside a transition.
 */
float director_progress(const Director* d);

/**
 * @brief Read eased transition progress for visual blending.
 *
 * @param d Director to inspect.
 * @return Cosine-eased weight in 0..1, or zero outside a transition.
 */
float director_mix(const Director* d);
