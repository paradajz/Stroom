#include "milkdrop/preset.h"
#include <math.h>
#include <string.h>

#define SINE_TABLE_STEPS 1024
#define SINE_TABLE_MASK  (SINE_TABLE_STEPS - 1)

/* Integer avalanche constants; preserve the hash sequence. */
#define HASH_INDEX_MULTIPLIER 0x9e3779b9u
#define HASH_MIX_MULTIPLIER_1 0x7feb352du
#define HASH_MIX_MULTIPLIER_2 0x846ca68bu
#define HASH_UNIT_SCALE       (1.0f / 16777216.0f)

// Keep lookup indices small enough to retain fractional interpolation precision.
#define LOOKUP_ANGLE_LIMIT 8192.0f

static float sine[SINE_TABLE_STEPS + 1];
static int   initialized;

float preset_hash(uint32_t seed, unsigned index)
{
    uint32_t x = seed + HASH_INDEX_MULTIPLIER * (index + 1);

    x ^= x >> 16;
    x *= HASH_MIX_MULTIPLIER_1;
    x ^= x >> 15;
    x *= HASH_MIX_MULTIPLIER_2;
    x ^= x >> 16;

    return (x >> 8) * HASH_UNIT_SCALE;
}

void preset_init(Preset* w, PresetKind kind, uint32_t seed)
{
    preset_init_at(w, kind, seed, 0, 0);
}

void preset_init_at(Preset* w, PresetKind kind, uint32_t seed, float time, unsigned frame)
{
    if (!initialized)
    {
        for (unsigned i = 0; i <= SINE_TABLE_STEPS; ++i)
        {
            sine[i] = sinf(PRESET_TAU * i / SINE_TABLE_STEPS);
        }

        initialized = 1;
    }

    memset(w, 0, sizeof(*w));

    w->kind = kind;
    w->seed = seed;

    milk_init_at(w, time, frame);
}

void preset_step(Preset* w, const MusicFeatures* audio, float seconds)
{
    if (!isfinite(seconds) || seconds <= 0)
    {
        return;
    }

    milk_step(w, audio, seconds);
}

/**
 * @brief Interpolate the sine table for a bounded angle.
 * @param angle Finite radians within LOOKUP_ANGLE_LIMIT plus one quarter-turn.
 * @return Interpolated sine.
 */
static float lookup_sin(float angle)
{
    float x = angle * (SINE_TABLE_STEPS / PRESET_TAU);
    int   i = (int)x;

    if (x < i)
    {
        --i;
    }

    unsigned j = (unsigned)i & SINE_TABLE_MASK;

    return sine[j] + (x - i) * (sine[j + 1] - sine[j]);
}

float preset_sin(float angle)
{
    if (!(fabsf(angle) <= LOOKUP_ANGLE_LIMIT))
    {
        return isfinite(angle) ? sinf(angle) : 0;
    }

    return lookup_sin(angle);
}

float preset_cos(float angle)
{
    if (!(fabsf(angle) <= LOOKUP_ANGLE_LIMIT))
    {
        /* Adding a quarter-turn loses the offset at large float magnitudes. */
        return isfinite(angle) ? cosf(angle) : 1;
    }

    return lookup_sin(angle + PRESET_TAU * .25f);
}

void preset_triangle(PresetCanvas* c, PresetVertex a, PresetVertex b, PresetVertex d, float ink)
{
    if (c->opacity > 0 && ink > 0)
    {
        c->triangle(c->context, a, b, d, c->opacity * ink);
    }
}

void preset_quad(PresetCanvas* c, PresetVertex a, PresetVertex b, PresetVertex d, PresetVertex e, float ink)
{
    preset_triangle(c, a, b, d, ink);
    preset_triangle(c, a, d, e, ink);
}

void preset_line(PresetCanvas* c, PresetVertex a, PresetVertex b, float ink)
{
    if (c->opacity > 0 && ink > 0)
    {
        c->line(c->context, a, b, c->opacity * ink);
    }
}

void preset_blend(PresetCanvas* c, int additive)
{
    if (c->blend)
    {
        c->blend(c->context, additive);
    }
}

const char* preset_name(PresetKind kind)
{
    return kind < MILK_PRESET_COUNT ? milk_programs[kind].name : "UNKNOWN";
}

void preset_draw(const Preset* w, PresetCanvas* c, const MusicFeatures* audio)
{
    preset_blend(c, 0);
    milk_draw(w, c, audio);
    preset_blend(c, 0);
}
