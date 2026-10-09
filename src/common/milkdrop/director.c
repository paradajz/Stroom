#include "contracts/milkdrop.h"
#include "util/random.h"
#include "milkdrop/director.h"
#include <math.h>
#include <string.h>

#define DIRECTOR_FALLBACK_SEED 0x279ba12u
#define VARIATION_STEPS        1000
#define VARIATION_STEP_SCALE   .001f
#define RANDOM_UNIT_SCALE      (1.0f / 16777216.0f)

_Static_assert(MILK_PRESET_COUNT > 0, "Benchmark at least one okay MilkDrop preset");

/**
 * @brief Advance the director xorshift generator.
 *
 * @param d Director whose RNG is updated.
 * @return Next 32-bit random value.
 */
static uint32_t random_next(Director* d)
{
    return d->random = util_random_next_u32(d->random);
}

/**
 * @brief Sample a uniform fraction from the director RNG.
 *
 * @param d Director whose RNG is updated.
 * @return Value in [0, 1).
 */
static float random_unit(Director* d)
{
    return (random_next(d) >> 8) * RANDOM_UNIT_SCALE;
}

/**
 * @brief Reset elapsed time and sample one extra preset delay.
 *
 * @param d Director whose timer and RNG are updated.
 */
static void restart_interval(Director* d)
{
    d->elapsed = 0;

    /* Match the reference's uniform 0..999/1000 fraction of the maximum. */
    d->interval_extra = d->variation ? d->variation * (random_next(d) % VARIATION_STEPS) * VARIATION_STEP_SCALE : 0;
}

static int eligible(const Director* d, PresetKind kind)
{
    return !d->frame_rate || milk_programs[kind].max_frame_rate >= d->frame_rate;
}

static PresetKind adjacent(const Director* d, PresetKind kind, int direction)
{
    do
    {
        kind = (kind + (direction > 0 ? 1 : MILK_PRESET_COUNT - 1)) % MILK_PRESET_COUNT;
    } while (!eligible(d, kind));

    return kind;
}

/** Shuffle eligible presets exactly once into a fixed-size playback order. */
static void shuffle_order(Director* d)
{
    d->order_count = 0;

    for (unsigned i = 0; i < MILK_PRESET_COUNT; ++i)
    {
        if (eligible(d, i))
        {
            d->order[d->order_count++] = (PresetKind)i;
        }
    }

    for (unsigned count = d->order_count; count > 1; --count)
    {
        unsigned   at   = random_next(d) % count;
        PresetKind swap = d->order[count - 1];

        d->order[count - 1] = d->order[at];
        d->order[at]        = swap;
    }

    d->order_at = 0;
}

/** Count the selected preset as the first entry of a fresh shuffle cycle. */
static void restart_order(Director* d, PresetKind first)
{
    shuffle_order(d);

    for (unsigned i = 0; i < d->order_count; ++i)
    {
        if (d->order[i] == first)
        {
            d->order[i] = d->order[0];
            d->order[0] = first;

            break;
        }
    }

    d->order_at = 1;
}

void director_init(Director* d, uint32_t seed)
{
    memset(d, 0, sizeof(*d));

    d->random             = seed ? seed : DIRECTOR_FALLBACK_SEED;
    d->mode               = DIRECTOR_SHUFFLE;
    d->hard_cut_threshold = 5;

    shuffle_order(d);

    PresetKind first       = d->order[d->order_at++];
    uint32_t   preset_seed = random_next(d);

    preset_init(&d->current, first, preset_seed);

    d->duration  = 10;
    d->variation = 3;

    restart_interval(d);
}

/**
 * @brief Initialize the destination preset and choose its blend duration.
 *
 * @param d Director to update.
 * @param kind Destination preset kind.
 */
static void begin_transition(Director* d, PresetKind kind)
{
    preset_init_at(&d->next, kind, random_next(d), d->time, d->frame);

    d->blend_duration = .65f + .55f * random_unit(d);
    d->blend_time     = 0;
    d->transitioning  = 1;
}

void director_next(Director* d)
{
    unsigned count = d->order_count;

    if (d->transitioning || count < 2)
    {
        return;
    }

    if (d->mode == DIRECTOR_SEQUENTIAL)
    {
        begin_transition(d, adjacent(d, d->current.kind, 1));
        return;
    }

    if (d->order_at == d->order_count)
    {
        shuffle_order(d);
        /* Avoid replaying the last preset at the boundary between cycles. */

        if (d->order[0] == d->current.kind)
        {
            unsigned at = 1 + random_next(d) % (count - 1);

            d->order[0]  = d->order[at];
            d->order[at] = d->current.kind;
        }
    }

    begin_transition(d, d->order[d->order_at++]);
}

void director_move(Director* d, int direction)
{
    if (!direction || d->order_count < 2)
    {
        return;
    }

    if (d->transitioning)
    {
        d->current       = d->next;
        d->transitioning = 0;
    }

    int kind = adjacent(d, d->current.kind, direction);

    begin_transition(d, (PresetKind)kind);

    if (d->mode == DIRECTOR_SHUFFLE)
    {
        restart_order(d, (PresetKind)kind);
    }

    d->elapsed = 0;
}

int director_set_frame_rate(Director* d, int frame_rate)
{
    if (frame_rate != MILKDROP_FPS_BASELINE && frame_rate != MILKDROP_FPS_HIGH)
    {
        return DIRECTOR_ERROR_FRAME_RATE;
    }

    unsigned count = 0;

    for (unsigned i = 0; i < MILK_PRESET_COUNT; ++i)
    {
        count += milk_programs[i].max_frame_rate >= frame_rate;
    }

    if (!count)
    {
        return DIRECTOR_ERROR_NO_ELIGIBLE_PRESETS;
    }

    d->frame_rate    = frame_rate;
    d->transitioning = 0;

    if (!eligible(d, d->current.kind))
    {
        preset_init_at(&d->current, adjacent(d, d->current.kind, 1), random_next(d), d->time, d->frame);
    }

    restart_order(d, d->current.kind);
    restart_interval(d);

    return 0;
}

void director_set_mode(Director* d, DirectorMode mode)
{
    if (mode < DIRECTOR_FIXED || mode >= DIRECTOR_MODE_COUNT)
    {
        return;
    }

    if (mode == DIRECTOR_SHUFFLE && d->mode != DIRECTOR_SHUFFLE)
    {
        restart_order(d, d->transitioning ? d->next.kind : d->current.kind);
    }

    d->mode = mode;

    restart_interval(d);

    if (mode == DIRECTOR_FIXED)
    {
        d->transitioning = 0;
    }
}

void director_adjust_duration(Director* d, int direction)
{
    if (!direction)
    {
        return;
    }

    d->duration = fminf(DIRECTOR_MAX_SECONDS, fmaxf(DIRECTOR_MIN_SECONDS, d->duration + (direction > 0 ? 5 : -5)));

    restart_interval(d);
}

void director_adjust_variation(Director* d, int direction)
{
    if (!direction)
    {
        return;
    }

    int value = (int)d->variation + (direction > 0 ? 1 : -1);

    d->variation = value < 0 ? 0 : value > 10 ? 10
                                              : value;

    restart_interval(d);
}

const char* director_mode_name(DirectorMode mode)
{
    return mode == DIRECTOR_SEQUENTIAL ? "SEQUENTIAL" : mode == DIRECTOR_SHUFFLE ? "SHUFFLE"
                                                                                 : "FIXED";
}

float director_progress(const Director* d)
{
    return d->transitioning ? fminf(1, fmaxf(0, d->blend_time / d->blend_duration)) : 0;
}

float director_mix(const Director* d)
{
    if (!d->transitioning)
    {
        return 0;
    }

    float t = director_progress(d);

    return .5f - .5f * cosf(t * PRESET_TAU * .5f);
}

void director_step(Director* d, const MusicFeatures* audio, float dt)
{
    if (!isfinite(dt) || dt <= 0)
    {
        return;
    }

    d->time  = audio->time;
    d->frame = audio->frame;

    preset_step(&d->current, audio, dt);

    d->elapsed += dt;

    if (d->transitioning)
    {
        preset_step(&d->next, audio, dt);

        d->blend_time += dt;

        if (d->blend_time >= d->blend_duration)
        {
            d->current       = d->next;
            d->transitioning = 0;

            restart_interval(d);
        }
    }
    else if (d->order_count > 1 && d->mode != DIRECTOR_FIXED && d->hard_cuts && (audio->relative[0] + audio->relative[1] + audio->relative[2]) > d->hard_cut_threshold * 3)
    {
        director_next(d);

        d->current       = d->next;
        d->transitioning = 0;

        restart_interval(d);

        d->hard_cut_threshold *= 2;
    }
    else
    {
        if (d->mode != DIRECTOR_FIXED && d->hard_cuts)
        {
            d->hard_cut_threshold = 2.5f + (d->hard_cut_threshold - 2.5f) * expf(-1.3863f * dt / 60);
        }

        if (d->mode != DIRECTOR_FIXED && d->elapsed >= d->duration + d->interval_extra)
        {
            director_next(d);
        }
    }
}
