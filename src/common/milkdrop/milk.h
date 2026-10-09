#pragma once

#include <stdint.h>
#include <math.h>
#include "milkdrop/music.h"
#include "milk_presets.h"
#include "milkdrop/milk_objects.h"

/* Largest float32 integer safely convertible to an unsigned equation operand. */
#define MILK_EQUATION_UINT32_MAX 4294967040.0f

#define MILK_WAVE_MAX      MILK_AUDIO_SAMPLES
#define MILK_MOTION_X      64
#define MILK_MOTION_Y      48
#define MILK_CENTER_RADIUS 12.8f
#define MILK_CENTER_ALPHA  (3.0f / 32)

/** @brief Fixed spatial inputs supplied to per-vertex equations. */
typedef struct
{
    float x;      /**< Normalized horizontal coordinate. */
    float y;      /**< Aspect-corrected vertical coordinate. */
    float radius; /**< Distance from the viewport center. */
    float angle;  /**< Angle around the viewport center in radians. */
} MilkVertexInput;

/**
 * @brief Persistent frame, vertex, and custom-object equation state.
 */
typedef struct
{
    float           frame[MILK_VARIABLES];       /**< Persistent per-frame equation variable pool. */
    float           vertex[MILK_VARIABLES];      /**< Persistent per-vertex equation variable pool. */
    float           initial_q[MILK_Q_VARIABLES]; /**< q variables saved after initialization. */
    float           inputs[MILK_FRAME_INPUTS];   /**< Read-only shared time and audio inputs. */
    uint32_t        random;                      /**< Mutable equation RNG state. */
    MilkObjectState objects[MILK_OBJECTS];       /**< Per-object equation state and cached geometry. */
} MilkState;

/**
 * @brief Immutable compiled preset equations and defaults; pointers are borrowed.
 */
typedef struct
{
    const char* name;                         /**< Borrowed preset display name. */
    float       defaults[MILK_OUTPUT_COUNT];  /**< Initial values for built-in equation outputs. */
    void (*init)(float*, uint32_t*);          /**< Initialization callback receiving variables and RNG state; returns nothing. */
    void (*frame)(float*, uint32_t*);         /**< Per-frame callback receiving variables and RNG state; returns nothing. */
    void (*vertex)(float*, uint32_t*);        /**< Per-vertex callback receiving variables and RNG state; returns nothing. */
    unsigned                 object_count;    /**< Number of custom shape and wave programs. */
    const MilkObjectProgram* objects;         /**< Borrowed array of object_count compiled object programs. */
    int                      custom_spectrum; /**< Enabled custom waves require stereo spectra. */
    int                      max_frame_rate;  /**< Build-time eligibility: highest nominal playback rate, or zero if ineligible. */
} MilkProgram;

extern const MilkProgram milk_programs[];

/**
 * @brief Presentation echo mapping and opacity.
 */
typedef struct
{
    float    zoom;        /**< Echo zoom factor. */
    float    alpha;       /**< Echo opacity weight. */
    unsigned orientation; /**< Flip bits: bit 0 horizontal, bit 1 vertical. */
    int      wrap;        /**< Nonzero to wrap texture coordinates instead of clamping. */
} MilkEcho;

/**
 * @brief Presentation effects applied without feeding them into raw artwork.
 */
typedef struct
{
    float    base;        /**< Base artwork weight. */
    float    gamma;       /**< Presentation brightness multiplier. */
    int      brighten;    /**< Nonzero to apply classic invert-square-invert before darken. */
    int      darken;      /**< Nonzero to square final RGB channels. */
    int      solarize;    /**< Nonzero for classic inverse modulation and doubling after darken. */
    int      invert;      /**< Nonzero to invert final colors. */
    MilkEcho echo[2];     /**< Outgoing and incoming preset echo passes. */
    float    shade[4][3]; /**< RGB multipliers at top-left, top-right, bottom-left, and bottom-right. */
    int      shaded;      /**< Nonzero if corner modulation differs from neutral white. */
} MilkComposite;

/**
 * @brief One pixel-space waveform position.
 */
typedef struct
{
    float x; /**< Horizontal pixel coordinate. */
    float y; /**< Vertical pixel coordinate. */
} MilkPoint;

/**
 * @brief Classic waveform geometry and discontinuities.
 */
typedef struct
{
    MilkPoint point[MILK_WAVE_MAX]; /**< Pixel-space waveform points. */
    unsigned  count;                /**< Number of valid waveform points. */
    unsigned  break_at;             /**< First discontinuity index; no segment crosses this point. */
    unsigned  break_at2;            /**< Second discontinuity index; no segment crosses this point. */
    int       closed;               /**< Nonzero when the final point should connect to the first. */
} MilkWave;

/**
 * @brief Motion-vector grid and appearance.
 */
typedef struct
{
    float x;      /**< Horizontal motion-vector grid count. */
    float y;      /**< Vertical motion-vector grid count. */
    float dx;     /**< Normalized horizontal grid offset. */
    float dy;     /**< Normalized vertical grid offset. */
    float length; /**< Vector displacement multiplier. */
    float r;      /**< Red component in 0..1. */
    float g;      /**< Green component in 0..1. */
    float b;      /**< Blue component in 0..1. */
    float alpha;  /**< Vector opacity in 0..1. */
} MilkMotion;

struct Preset;
struct FeedbackTransform;
struct PresetCanvas;

/**
 * @brief Build classic waveform geometry in the shared viewport.
 *
 * Classic modes use 480 aligned samples with lookahead in the 576-sample buffer.
 *
 * @param p MilkDrop preset with waveform settings.
 * @param audio Audio features for spectrum-based modes.
 * @param wave Smoothed stereo waveform samples.
 * @param shape Destination points and discontinuity markers.
 */
void milk_wave_shape(const struct Preset* p, const MusicFeatures* audio, const float wave[2][MILK_AUDIO_SAMPLES], MilkWave* shape);

/**
 * @brief Classic waveform color and rasterization options.
 */
typedef struct
{
    float r;        /**< Red component in 0..1. */
    float g;        /**< Green component in 0..1. */
    float b;        /**< Blue component in 0..1. */
    float alpha;    /**< Waveform opacity in 0..1. */
    int   dots;     /**< Nonzero to draw points instead of lines. */
    int   thick;    /**< Nonzero to use thicker strokes. */
    int   additive; /**< Nonzero to use additive blending. */
} MilkWaveStyle;

/**
 * @brief Resolve classic waveform color, opacity, and drawing flags.
 *
 * @param p MilkDrop preset.
 * @param audio Current audio features.
 * @param style Destination waveform style.
 */
void milk_wave_style(const struct Preset* p, const MusicFeatures* audio, MilkWaveStyle* style);

/**
 * @brief Resample and blend wave shapes while preserving stereo discontinuities.
 *
 * @param old Outgoing shape.
 * @param next Incoming shape.
 * @param mix Incoming weight; values outside 0..1 select an endpoint.
 * @param out Destination shape.
 */
void milk_wave_morph(const MilkWave* old, const MilkWave* next, float mix, MilkWave* out);

/**
 * @brief Draw a classic waveform, optionally morphing to another preset.
 *
 * @param old Outgoing MilkDrop preset.
 * @param next Incoming MilkDrop preset, or NULL.
 * @param mix Incoming weight in 0..1.
 * @param c Drawing sink.
 * @param audio Current audio features.
 */
void milk_wave_draw(const struct Preset* old, const struct Preset* next, float mix, struct PresetCanvas* c, const MusicFeatures* audio);

/**
 * @brief Blend MilkDrop objects, waveform, and decorations between two presets.
 *
 * @param old Outgoing MilkDrop preset.
 * @param next Incoming MilkDrop preset.
 * @param mix Incoming weight in 0..1.
 * @param c Drawing sink.
 * @param audio Current audio features.
 */
void milk_draw_transition(const struct Preset* old, const struct Preset* next, float mix, struct PresetCanvas* c, const MusicFeatures* audio);

/**
 * @brief Scale and spatially smooth both reference waveforms for drawing.
 *
 * The spatial filter restarts at the first sample each frame; reference amplitudes are divided by 128.
 *
 * @param audio Reference audio features.
 * @param scale Waveform gain.
 * @param smoothing Smoothing coefficient, clamped to 0..1.
 * @param wave Destination stereo samples, normalized from reference amplitude units.
 */
void milk_smooth_wave(const MusicFeatures* audio, float scale, float smoothing, float wave[2][MILK_AUDIO_SAMPLES]);

/**
 * @brief Resolve presentation-only echo, color shading, and gain.
 *
 * @param a Outgoing preset, or NULL for neutral settings.
 * @param b Incoming preset, or NULL.
 * @param mix Incoming weight, clamped to 0..1.
 * @param composite Destination composite settings.
 */
void milk_composite(const struct Preset* a, const struct Preset* b, float mix, MilkComposite* composite);

/**
 * @brief Interpolate the presentation color across the viewport triangles.
 *
 * @param composite Composite corner colors.
 * @param x Normalized horizontal coordinate.
 * @param y Normalized vertical coordinate.
 * @param rgb Destination RGB multipliers.
 */
void milk_shade_at(const MilkComposite* composite, float x, float y, float rgb[3]);

/**
 * @brief Map a screen position through echo zoom and orientation.
 *
 * @param echo Echo settings.
 * @param x Normalized horizontal screen coordinate.
 * @param y Normalized vertical screen coordinate.
 * @param u Destination horizontal texture coordinate.
 * @param v Destination vertical texture coordinate.
 */
void milk_echo_uv(const MilkEcho* echo, float x, float y, float* u, float* v);

/**
 * @brief Initialize MilkDrop equations and objects against the shared clock.
 *
 * @param p Preset with kind and seed already set.
 * @param time Shared time in seconds.
 * @param frame Shared frame number.
 */
void milk_init_at(struct Preset* p, float time, unsigned frame);

/**
 * @brief Execute frame and custom-object equations with current audio inputs.
 *
 * @param p MilkDrop preset to update.
 * @param a Audio features and shared clock.
 * @param dt Positive frame duration in seconds.
 */
void milk_step(struct Preset* p, const MusicFeatures* a, float dt);

/**
 * @brief Copy frame inputs into the persistent per-vertex equation state.
 *
 * @param p MilkDrop preset to prepare.
 */
void milk_begin_vertices(struct Preset* p);

/**
 * @brief Run vertex equations and derive a feedback transform.
 *
 * @param p Preset whose vertex variables and RNG may advance.
 * @param input Precomputed spatial inputs, copied before equations run.
 * @param f Destination feedback transform.
 */
void milk_transform(struct Preset* p, const MilkVertexInput* input, struct FeedbackTransform* f);

/**
 * @brief Derive feedback settings without running per-vertex equations.
 *
 * @param p MilkDrop preset.
 * @param f Destination transform.
 */
void milk_frame_transform(const struct Preset* p, struct FeedbackTransform* f);

/* Keep these pure helpers visible to generated equations so the compiler can
 * remove call overhead and reuse expressions without relaxing math semantics. */

/**
 * @brief Replace non-finite equation results with zero.
 *
 * @param x Value to sanitize.
 * @return x if finite, otherwise zero.
 */
static inline float milk_finite(float x)
{
    return isfinite(x) ? x : 0;
}

/**
 * @brief Replace non-finite input with zero and clamp to a range.
 *
 * @param x Input value.
 * @param lo Finite lower bound, no greater than hi.
 * @param hi Finite upper bound.
 * @return Finite, clamped value.
 */
static inline float milk_bound(float x, float lo, float hi)
{
    return fminf(hi, fmaxf(lo, milk_finite(x)));
}

/**
 * @brief Approximate sine for compiled equations.
 *
 * @param x Angle in radians.
 * @return Approximate trigonometric value.
 */
float milk_fast_sin(float x);

/**
 * @brief Approximate cosine for compiled equations.
 *
 * @param x Angle in radians.
 * @return Approximate trigonometric value.
 */
float milk_fast_cos(float x);

/**
 * @brief Compute approximate sine and cosine with shared angle reduction.
 *
 * Results match the individual sine and cosine helper paths.
 *
 * @param x Angle in radians.
 * @param sine Destination sine.
 * @param cosine Destination cosine.
 */
void milk_fast_sincos(float x, float* sine, float* cosine);

/**
 * @brief Apply the EEL truth threshold.
 *
 * @param x Value to test.
 * @return 1 if absolute magnitude exceeds 0.00001; otherwise 0.
 */
static inline int milk_truth(float x)
{
    return fabsf(x) > .00001f;
}

/**
 * @brief Compare values using the EEL equality tolerance.
 *
 * @param a First operand.
 * @param b Second operand.
 * @return 1 if the absolute difference is below 0.00001; otherwise 0.
 */
static inline float milk_equal(float a, float b)
{
    return fabsf(a - b) < .00001f;
}

/**
 * @brief Divide equation operands with finite-result protection.
 *
 * @param a Numerator.
 * @param b Denominator.
 * @return Quotient, or zero for a zero divisor or non-finite result.
 */
static inline float milk_div(float a, float b)
{
    return b == 0 ? 0 : milk_finite(a / b);
}

/**
 * @brief Compute EEL-style remainder using bounded, absolute integer operands.
 *
 * @param a Dividend.
 * @param b Divisor.
 * @return Unsigned integer remainder as a float, or zero for a zero converted divisor.
 */
static inline float milk_mod(float a, float b)
{
    uint32_t divisor = (uint32_t)fminf(MILK_EQUATION_UINT32_MAX, fabsf(b));
    uint32_t value   = (uint32_t)fminf(MILK_EQUATION_UINT32_MAX, fabsf(a));

    return divisor ? (float)(value % divisor) : 0;
}

/**
 * @brief Raise a value to a power with finite-result protection.
 *
 * @param a Base.
 * @param b Exponent.
 * @return Power, or zero if the result is non-finite.
 */
float milk_pow(float a, float b);

/**
 * @brief Apply bitwise AND after conversion to signed 64-bit integers.
 *
 * @param a First operand.
 * @param b Second operand.
 * @return Integer result converted to float.
 */
float milk_bitand(float a, float b);

/**
 * @brief Apply bitwise OR after conversion to signed 64-bit integers.
 *
 * @param a First operand.
 * @param b Second operand.
 * @return Integer result converted to float.
 */
float milk_bitor(float a, float b);

/**
 * @brief Compute the square root of the absolute input.
 *
 * @param x Input value.
 * @return Square root of abs(x).
 */
float milk_sqrt(float x);

/**
 * @brief Square an equation value with finite-result protection.
 *
 * @param x Input value.
 * @return Square, or zero if non-finite.
 */
float milk_sqr(float x);

/**
 * @brief Evaluate the sign of an equation value.
 *
 * @param x Input value.
 * @return -1 for negative, 1 for positive, or 0 otherwise.
 */
static inline float milk_sign(float x)
{
    return x > 0 ? 1 : x < 0 ? -1
                             : 0;
}

/**
 * @brief Compute inverse sine after clamping the input to -1..1.
 *
 * @param x Input value.
 * @return Angle in radians.
 */
float milk_asin(float x);

/**
 * @brief Compute inverse cosine after clamping the input to -1..1.
 *
 * @param x Input value.
 * @return Angle in radians.
 */
float milk_acos(float x);

/**
 * @brief Compute the exponential with finite-result protection.
 *
 * @param x Input value.
 * @return Function result, or zero if non-finite.
 */
float milk_exp(float x);

/**
 * @brief Compute the natural logarithm with finite-result protection.
 *
 * @param x Input value.
 * @return Function result, or zero if non-finite.
 */
float milk_log(float x);

/**
 * @brief Compute the base-10 logarithm with finite-result protection.
 *
 * @param x Input value.
 * @return Function result, or zero if non-finite.
 */
float milk_log10(float x);

/**
 * @brief Evaluate a logistic curve.
 *
 * @param a Input value.
 * @param b Slope multiplier.
 * @return 1 / (1 + exp(-a * b)).
 */
float milk_sigmoid(float a, float b);

/**
 * @brief Advance the equation RNG and select a discrete value.
 *
 * @param state Mutable xorshift state; initialize with a nonzero seed.
 * @param range Exclusive upper bound, truncated and raised to at least 1.
 * @return Integer-valued float in [0, max(1, trunc(range))).
 */
float milk_rand(uint32_t* state, float range);
