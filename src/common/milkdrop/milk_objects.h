#pragma once

#include <stdint.h>
#include "milk_presets.h"

#define MILK_OBJECT_SAMPLES 512

/**
 * @brief Custom-object vertex whose attributes survive screen and texture clipping.
 */
typedef struct
{
    float x; /**< Horizontal pixel coordinate. */
    float y; /**< Vertical pixel coordinate. */
    float r; /**< Red component on a 0..255 scale. */
    float g; /**< Green component on a 0..255 scale. */
    float b; /**< Blue component on a 0..255 scale. */
    float a; /**< Opacity in 0..1. */
    float u; /**< Normalized horizontal texture coordinate. */
    float v; /**< Normalized vertical texture coordinate. */
} MilkVertex;

typedef struct MilkObjectState MilkObjectState;

/**
 * @brief Immutable compiled custom shape or wave program.
 */
typedef struct
{
    unsigned type;                                                                   /**< Object type: 0 shape, 1 wave. */
    unsigned index;                                                                  /**< Original object index in the preset. */
    float    defaults[MILK_OBJECT_FIELDS];                                           /**< Initial built-in object field values. */
    void (*init)(float*, uint32_t*);                                                 /**< Initialization callback receiving variables and RNG state; returns nothing. */
    void (*frame)(float*, uint32_t*);                                                /**< Frame callback receiving variables and RNG state; returns nothing. */
    void (*points)(MilkObjectState*, const float[2][MILK_OBJECT_SAMPLES], unsigned); /**< Evaluate a complete wave from prepared stereo samples. */
} MilkObjectProgram;

/**
 * @brief Persistent custom-object equations and per-frame geometry cache.
 */
struct MilkObjectState
{
    float    frame[MILK_OBJECT_VARIABLES]; /**< Persistent per-frame object variables. */
    float    point[MILK_OBJECT_VARIABLES]; /**< Persistent per-point wave variables. */
    float    initial_t[MILK_T_VARIABLES];  /**< t variables saved after initialization. */
    uint32_t random;                       /**< Independent mutable object RNG state. */
    unsigned count;                        /**< Valid shape-instance or wave-point count. */

    /**
     * @brief Geometry storage shared by mutually exclusive shape and wave types.
     */
    union
    {
        float      shape[MILK_OBJECT_INSTANCES][MILK_OBJECT_FIELDS]; /**< Evaluated fields for each shape instance. */
        MilkVertex wave[MILK_OBJECT_SAMPLES];                        /**< Evaluated pixel-space custom wave vertices. */
    } cache;                                                         /**< Storage interpreted according to the compiled object type. */
};

struct Preset;
struct PresetCanvas;
struct MusicFeatures;

/**
 * @brief Initialize custom shape and wave equation state.
 *
 * @param p MilkDrop preset to initialize.
 */
void milk_objects_init(struct Preset* p);

/**
 * @brief Run object frame and point equations and cache drawable geometry.
 *
 * @param p MilkDrop preset to update.
 * @param audio Current waveform and spectrum features.
 */
void milk_objects_step(struct Preset* p, const struct MusicFeatures* audio);

/**
 * @brief Draw cached custom shapes and waves.
 *
 * @param p MilkDrop preset.
 * @param canvas Drawing sink.
 */
void milk_objects_draw(const struct Preset* p, struct PresetCanvas* canvas);

/**
 * @brief Clip and emit a custom triangle, preserving color and texture attributes.
 *
 * @param canvas Drawing sink.
 * @param vertices Three triangle vertices.
 * @param textured Nonzero to sample the feedback texture.
 * @param wrap Nonzero to wrap texture coordinates instead of clamping.
 */
void milk_object_triangle(struct PresetCanvas* canvas, const MilkVertex* vertices, int textured, int wrap);

/**
 * @brief Clip and emit a custom line within the viewport.
 *
 * @param canvas Drawing sink.
 * @param a Start vertex.
 * @param b End vertex.
 */
void milk_object_line(struct PresetCanvas* canvas, MilkVertex a, MilkVertex b);

/**
 * @brief Draw two smoothed wave segments with optional thickness copies.
 *
 * Thickness offsets preserve the original offset-then-interpolate arithmetic and endpoint rounding.
 *
 * @param canvas Drawing sink.
 * @param a Start vertex.
 * @param mid Smoothed midpoint.
 * @param b End vertex.
 * @param copies Number of offset copies: 1 for thin, 4 for thick.
 */
void milk_object_wave_segment(struct PresetCanvas* canvas, MilkVertex a, MilkVertex mid, MilkVertex b, unsigned copies);
