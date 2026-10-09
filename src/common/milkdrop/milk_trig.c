/* Fast equation trig only. Audio FFTs and renderer geometry retain libm. */
#include "contracts/milkdrop.h"
#include "milkdrop/milk.h"

/* Split-turn constants retain the original float32 reduction order. */
#define TRIG_SIMPLE_REDUCTION_LIMIT 8192
#define TRIG_INVERSE_TAU            .15915494309189533577f
#define TRIG_TAU_HIGH               6.28125f
#define TRIG_TAU_LOW                .00193530717958647693f
#define TRIG_TAU_MIDDLE             .00193500518798828125f
#define TRIG_TAU_TAIL               3.0199159795074463e-7f
#define TRIG_TURN_BLOCK             256
#define TRIG_PI                     3.14159265358979323846f

/**
 * @brief Reduce a bounded angle using split constants to limit float cancellation.
 *
 * @param x Angle in radians with absolute value at most 2^20.
 * @return Approximately equivalent angle in [-pi, pi].
 */
static float reduce_angle(float x)
{
    /* Splitting 2*pi makes the large subtraction exact in the supported range.
     * A single float 2*pi accumulates visible phase error at larger angles. */
    int turns = (int)(x * TRIG_INVERSE_TAU + (x < 0 ? -.5f : .5f));

    if (fabsf(x) <= TRIG_SIMPLE_REDUCTION_LIMIT)
    {
        return (x - turns * TRIG_TAU_HIGH) - turns * TRIG_TAU_LOW;
    }

    /* Split both the turn count and 2*pi so the larger products stay exact
     * in float32 through |x| <= 2^20. Keep this subtraction order. */
    int   high = (turns / TRIG_TURN_BLOCK) * TRIG_TURN_BLOCK;
    int   low  = turns - high;
    float r    = x - high * TRIG_TAU_HIGH;

    r -= low * TRIG_TAU_HIGH;
    r -= high * TRIG_TAU_MIDDLE;
    r -= low * TRIG_TAU_MIDDLE;
    r -= turns * TRIG_TAU_TAIL;

    return r;
}

/**
 * @brief Evaluate the sine polynomial after folding into the central half-turn.
 *
 * @param x Angle in [-pi, pi].
 * @return Approximate sine.
 */
static float sine_small(float x)
{
    if (x > TRIG_PI * .5f)
    {
        x = TRIG_PI - x;
    }
    else if (x < -TRIG_PI * .5f)
    {
        x = -TRIG_PI - x;
    }

    float z = x * x;

    /* Odd Taylor polynomial through x^11 on [-pi/2,pi/2]. */
    return x + x * z * (-1.0f / 6 + z * (1.0f / 120 + z * (-1.0f / 5040 + z * (1.0f / 362880 + z * (-1.0f / 39916800)))));
}

float milk_fast_sin(float x)
{
    if (!(fabsf(x) <= MILKDROP_TRIG_FAST_LIMIT))
    {
        return sinf(x);
    }

    return sine_small(reduce_angle(x));
}

float milk_fast_cos(float x)
{
    if (!(fabsf(x) <= MILKDROP_TRIG_FAST_LIMIT))
    {
        return cosf(x);
    }

    float phase = reduce_angle(x) + TRIG_PI * .5f;

    if (phase > TRIG_PI)
    {
        phase -= 2 * TRIG_PI;
    }

    return sine_small(phase);
}

void milk_fast_sincos(float x, float* sine, float* cosine)
{
    if (!(fabsf(x) <= MILKDROP_TRIG_FAST_LIMIT))
    {
        *sine   = sinf(x);
        *cosine = cosf(x);

        return;
    }

    float phase = reduce_angle(x);

    *sine = sine_small(phase);
    phase += TRIG_PI * .5f;

    if (phase > TRIG_PI)
    {
        phase -= 2 * TRIG_PI;
    }

    *cosine = sine_small(phase);
}
