#pragma once

#include "milkdrop/preset.h"

#define FEEDBACK_X        20
#define FEEDBACK_Y        16
#define FEEDBACK_VERTICES ((FEEDBACK_X + 1) * (FEEDBACK_Y + 1))

/**
 * @brief Normalized coordinates into feedback history.
 */
typedef struct
{
    float u; /**< Horizontal normalized texture coordinate; may wrap outside 0..1. */
    float v; /**< Vertical normalized texture coordinate; may wrap outside 0..1. */
} FeedbackUV;

/**
 * @brief Frame or vertex feedback mapping in normalized texture space.
 */
typedef struct FeedbackTransform
{
    float zoom;           /**< Zoom factor. */
    float zoom_exp;       /**< Radial zoom exponent. */
    float rotation;       /**< Rotation in radians. */
    float cx;             /**< Normalized horizontal transform center. */
    float cy;             /**< Normalized vertical transform center. */
    float dx;             /**< Horizontal texture displacement. */
    float dy;             /**< Vertical texture displacement. */
    float sx;             /**< Horizontal scale factor. */
    float sy;             /**< Vertical scale factor. */
    float warp;           /**< Warp amount. */
    float warp_scale;     /**< Warp spatial scale. */
    float warp_time;      /**< Warp animation time. */
    float decay;          /**< Previous-artwork retention multiplier. */
    float oscillators[4]; /**< Precomputed warp oscillator coefficients. */
    int   wrap;           /**< Nonzero to repeat texture tiles. */
} FeedbackTransform;

/**
 * @brief Warped texture mapping for the fixed feedback grid.
 */
typedef struct
{
    FeedbackUV uv[FEEDBACK_VERTICES]; /**< Row-major normalized UVs for every grid vertex. */
    float      decay;                 /**< Previous-artwork retention multiplier. */
    int        wrap;                  /**< Nonzero to split and repeat texture tiles. */
} FeedbackMesh;

/**
 * @brief Evaluate a seeded spatial preset-transition mask.
 *
 * @param progress Linear transition progress in 0..1.
 * @param x Normalized horizontal position.
 * @param y Normalized vertical position.
 * @param seed Seed selecting the spatial transition pattern.
 * @return Incoming-preset weight in 0..1.
 */
float feedback_transition(float progress, float x, float y, uint32_t seed);

/**
 * @brief Build a feedback mesh, blending two presets when requested.
 *
 * @param a Outgoing preset; vertex equation state may advance.
 * @param b Incoming preset, or NULL; vertex state may advance.
 * @param mix Incoming blend weight in 0..1.
 * @param mesh Destination texture mapping and decay settings.
 */
void feedback_build(Preset* a, Preset* b, float mix, FeedbackMesh* mesh);

/**
 * @brief Triangle vertex used by feedback and echo drawing sinks.
 */
typedef struct
{
    float x; /**< Horizontal pixel coordinate. */
    float y; /**< Vertical pixel coordinate. */
    float u; /**< Normalized horizontal texture coordinate. */
    float v; /**< Normalized vertical texture coordinate. */
} FeedbackVertex;

typedef void (*FeedbackTriangle)(void*, const FeedbackVertex*);

/**
 * @brief Blend motion-vector settings from two presets.
 *
 * @param a Outgoing preset.
 * @param b Incoming preset, or NULL.
 * @param mix Incoming blend weight, clamped to 0..1.
 * @param motion Destination vector settings.
 * @return Nonzero if vector opacity and grid dimensions allow drawing.
 */
int milk_motion_parameters(const Preset* a, const Preset* b, float mix, MilkMotion* motion);

/**
 * @brief Draw motion vectors from the existing feedback mesh without rerunning equations.
 *
 * @param motion Vector appearance and grid settings.
 * @param mesh Feedback mapping to sample.
 * @param canvas Drawing sink.
 */
void milk_motion_draw(const MilkMotion* motion, const FeedbackMesh* mesh, PresetCanvas* canvas);

/**
 * @brief Triangulate feedback cells, splitting at texture boundaries.
 *
 * @param mesh Feedback mesh.
 * @param draw Triangle callback; vertices are valid only during the call.
 * @param context Opaque callback context.
 */
void feedback_emit(const FeedbackMesh* mesh, FeedbackTriangle draw, void* context);

/**
 * @brief Emit clipped triangles for an echo pass.
 *
 * The affine mapping starts with two triangles; both wrap and clamp edges are split.
 *
 * @param echo Echo mapping settings.
 * @param draw Triangle callback; vertices are valid only during the call.
 * @param context Opaque callback context.
 */
void feedback_echo_emit(const MilkEcho* echo, FeedbackTriangle draw, void* context);
