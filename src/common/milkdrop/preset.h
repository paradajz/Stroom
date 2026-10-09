#pragma once

#include "milkdrop/music.h"
#include "milkdrop/milk.h"

#define PRESET_TAU 6.28318530718f

/**
 * @brief Zero-based index into the compiled MilkDrop preset library.
 */
typedef unsigned PresetKind;

/**
 * @brief One compiled MilkDrop preset instance.
 */
typedef struct Preset
{
    PresetKind kind; /**< Library preset identifier. */
    uint32_t   seed; /**< Seed for preset equations. */
    MilkState  milk; /**< Compiled equation and custom-object state for MilkDrop presets. */
} Preset;

/**
 * @brief Pixel-space drawing vertex with RGB color.
 */
typedef struct
{
    float x; /**< Horizontal pixel coordinate. */
    float y; /**< Vertical pixel coordinate. */
    float r; /**< Red component on a 0..255 scale. */
    float g; /**< Green component on a 0..255 scale. */
    float b; /**< Blue component on a 0..255 scale. */
} PresetVertex;

/**
 * @brief Borrowed drawing callbacks supplied by the renderer or host tests.
 */
typedef struct PresetCanvas
{
    void* context;                                                                                                 /**< Opaque borrowed context forwarded to callbacks. */
    void (*triangle)(void*, PresetVertex, PresetVertex, PresetVertex, float);                                      /**< Draw context, three vertices, and opacity; returns nothing. */
    void (*line)(void*, PresetVertex, PresetVertex, float);                                                        /**< Draw context, two endpoints, and opacity; returns nothing. */
    void (*sprite)(void*, PresetVertex, float, float);                                                             /**< Draw context, center vertex, half-size in pixels, and opacity; returns nothing. */
    void (*blend)(void*, int);                                                                                     /**< Optional context and additive-enable callback; returns nothing. */
    float opacity;                                                                                                 /**< Multiplier applied to primitive opacity. */
    void (*darken_center)(void*, float);                                                                           /**< Optional context and opacity callback for center darkening; returns nothing. */
    void (*object_triangle)(void*, const MilkVertex*, int);                                                        /**< Optional context, three vertices, and textured flag callback; returns nothing. */
    void (*object_line)(void*, MilkVertex, MilkVertex);                                                            /**< Optional context and two custom vertices callback; returns nothing. */
    void (*object_wave_segments)(void*, const MilkVertex*, const MilkVertex*, const MilkVertex*, float, unsigned); /**< Optional context, start/mid/end vertices, opacity and thickness copies; draws fully visible segments. */
} PresetCanvas;

/**
 * @brief Initialize a preset with its clock starting at zero.
 *
 * @param w Preset to initialize.
 * @param kind Library preset identifier.
 * @param seed Seed for preset equations.
 */
void preset_init(Preset* w, PresetKind kind, uint32_t seed);

/**
 * @brief Initialize a preset against the shared visualization clock.
 *
 * @param w Preset to initialize.
 * @param kind Library preset identifier.
 * @param seed Seed for preset equations.
 * @param time Shared time in seconds.
 * @param frame Shared frame number.
 */
void preset_init_at(Preset* w, PresetKind kind, uint32_t seed, float time, unsigned frame);

/**
 * @brief Run preset frame equations for the elapsed time.
 *
 * @param w Preset to update.
 * @param audio Current audio features.
 * @param seconds Actual visible elapsed seconds; nonpositive or non-finite values are ignored.
 */
void preset_step(Preset* w, const MusicFeatures* audio, float seconds);

/**
 * @brief Derive a reproducible value without advancing random state.
 *
 * @param seed Base seed.
 * @param index Value index.
 * @return Deterministic value in [0, 1).
 */
float preset_hash(uint32_t seed, unsigned index);

/**
 * @brief Approximate sine using the table initialized by preset_init, with libm for large angles.
 *
 * @param angle Angle in radians.
 * @return Sine in [-1, 1]; zero for nonfinite angles.
 */
float preset_sin(float angle);

/**
 * @brief Approximate cosine using the table initialized by preset_init, with libm for large angles.
 *
 * @param angle Angle in radians.
 * @return Cosine in [-1, 1]; one for nonfinite angles.
 */
float preset_cos(float angle);

/**
 * @brief Emit a triangle with canvas opacity applied.
 *
 * @param c Drawing sink.
 * @param a First vertex.
 * @param b Second vertex.
 * @param d Third vertex.
 * @param ink Opacity multiplier.
 */
void preset_triangle(PresetCanvas* c, PresetVertex a, PresetVertex b, PresetVertex d, float ink);

/**
 * @brief Emit a quad as two triangles with canvas opacity applied.
 *
 * @param c Drawing sink.
 * @param a First vertex.
 * @param b Second vertex.
 * @param d Third vertex.
 * @param e Fourth vertex.
 * @param ink Opacity multiplier.
 */
void preset_quad(PresetCanvas* c, PresetVertex a, PresetVertex b, PresetVertex d, PresetVertex e, float ink);

/**
 * @brief Emit a line with canvas opacity applied.
 *
 * @param c Drawing sink.
 * @param a Start vertex.
 * @param b End vertex.
 * @param ink Opacity multiplier.
 */
void preset_line(PresetCanvas* c, PresetVertex a, PresetVertex b, float ink);

/**
 * @brief Select blending when the drawing sink supports it.
 *
 * @param c Drawing sink.
 * @param additive Nonzero for additive blending; zero for alpha blending.
 */
void preset_blend(PresetCanvas* c, int additive);

/**
 * @brief Draw the selected MilkDrop preset.
 *
 * @param w Preset to draw.
 * @param c Drawing sink.
 * @param audio Current audio features.
 */
void preset_draw(const Preset* w, PresetCanvas* c, const MusicFeatures* audio);

/**
 * @brief Draw custom objects, the classic waveform, and borders.
 *
 * @param p MilkDrop preset.
 * @param c Drawing sink.
 * @param audio Current audio features.
 */
void milk_draw(const Preset* p, PresetCanvas* c, const MusicFeatures* audio);

/**
 * @brief Get the display name for a library preset.
 *
 * @param kind Library preset identifier.
 * @return Static preset name, or UNKNOWN for an invalid identifier.
 */
const char* preset_name(PresetKind kind);
