#include "milkdrop/preset.h"
#include "milkdrop/feedback.h"
#include "unity.h"
#include "milkdrop/viewport.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned centers;

static unsigned draw_calls;

/**
 * @brief Feedback triangle coverage captured by rendering tests.
 */
typedef struct
{
    double   area;      /**< Sum of triangle areas in square pixels. */
    unsigned triangles; /**< Number of emitted triangles. */
} Capture;

/**
 * @brief Validate a feedback triangle and accumulate its count and area.
 *
 * @param ctx Capture to update.
 * @param v Three feedback vertices.
 */
static void triangle(void* ctx, const FeedbackVertex* v)
{
    Capture* c = ctx;

    ++c->triangles;

    for (unsigned i = 0; i < 3; ++i)
    {
        TEST_ASSERT_TRUE_MESSAGE(isfinite(v[i].x) && isfinite(v[i].y), "isfinite(v[i].x) && isfinite(v[i].y)");
        TEST_ASSERT_TRUE_MESSAGE(v[i].x >= -.01f && v[i].x <= (DISPLAY_WIDTH + .01f) && v[i].y >= -.01f && v[i].y <= (DISPLAY_HEIGHT + .01f), "v[i].x >= -.01f && v[i].x <= (DISPLAY_WIDTH + .01f) && v[i].y >= -.01f && v[i].y <= (DISPLAY_HEIGHT + .01f)");
        TEST_ASSERT_TRUE_MESSAGE(v[i].u >= -.00001f && v[i].u <= 1.00001f && v[i].v >= -.00001f && v[i].v <= 1.00001f, "v[i].u >= -.00001f && v[i].u <= 1.00001f && v[i].v >= -.00001f && v[i].v <= 1.00001f");
    }

    c->area += fabs((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x)) * .5;
}

/**
 * @brief Assert finite drawing attributes, viewport bounds, and valid opacity.
 *
 * @param v Vertex to validate.
 * @param alpha Opacity to validate.
 */
static void check_vertex(PresetVertex v, float alpha)
{
    TEST_ASSERT_TRUE_MESSAGE(isfinite(v.x) && isfinite(v.y) && isfinite(v.r) && isfinite(v.g) && isfinite(v.b), "isfinite(v.x) && isfinite(v.y) && isfinite(v.r) && isfinite(v.g) && isfinite(v.b)");
    TEST_ASSERT_TRUE_MESSAGE(v.x >= -2 && v.x <= 642 && v.y >= -2 && v.y <= 514, "v.x >= -2 && v.x <= 642 && v.y >= -2 && v.y <= 514");
    TEST_ASSERT_TRUE_MESSAGE(alpha >= 0 && alpha <= 1, "alpha >= 0 && alpha <= 1");
}

/**
 * @brief Validate and count a rendered triangle.
 *
 * @param context Unused callback context.
 * @param a First vertex.
 * @param b Second vertex.
 * @param c Third vertex.
 * @param alpha Opacity to validate.
 */
static void draw_triangle(void* context, PresetVertex a, PresetVertex b, PresetVertex c, float alpha)
{
    (void)context;
    check_vertex(a, alpha);
    check_vertex(b, alpha);
    check_vertex(c, alpha);
    ++draw_calls;
}

/**
 * @brief Validate and count a rendered line.
 *
 * @param context Unused callback context.
 * @param a Start vertex.
 * @param b End vertex.
 * @param alpha Opacity to validate.
 */
static void draw_line(void* context, PresetVertex a, PresetVertex b, float alpha)
{
    (void)context;
    check_vertex(a, alpha);
    check_vertex(b, alpha);
    ++draw_calls;
}

/**
 * @brief Validate and count a rendered sprite.
 *
 * @param context Unused callback context.
 * @param a Center vertex.
 * @param size Half-size in pixels.
 * @param alpha Opacity to validate.
 */
static void draw_sprite(void* context, PresetVertex a, float size, float alpha)
{
    (void)context;
    check_vertex(a, alpha);
    TEST_ASSERT_TRUE_MESSAGE(size > 0 && size <= 2, "size > 0 && size <= 2");
    ++draw_calls;
}

/**
 * @brief Validate a custom vertex through the shared draw assertions.
 *
 * @param v Custom vertex to validate.
 */
static void object_vertex(MilkVertex v)
{
    PresetVertex p = { v.x, v.y, v.r, v.g, v.b };

    check_vertex(p, v.a);
}

/**
 * @brief Validate and count a custom-object triangle.
 *
 * @param ctx Unused callback context.
 * @param v Three custom vertices.
 * @param textured Nonzero to also validate texture coordinates.
 */
static void object_triangle(void* ctx, const MilkVertex* v, int textured)
{
    (void)ctx;

    for (unsigned i = 0; i < 3; ++i)
    {
        object_vertex(v[i]);

        if (textured)
        {
            TEST_ASSERT_TRUE_MESSAGE(v[i].u >= 0 && v[i].u <= 1 && v[i].v >= 0 && v[i].v <= 1, "v[i].u >= 0 && v[i].u <= 1 && v[i].v >= 0 && v[i].v <= 1");
        }
    }

    ++draw_calls;
}

/**
 * @brief Validate and count a custom-object line.
 *
 * @param ctx Unused callback context.
 * @param a Start vertex.
 * @param b End vertex.
 */
static void object_line(void* ctx, MilkVertex a, MilkVertex b)
{
    (void)ctx;
    object_vertex(a);
    object_vertex(b);
    ++draw_calls;
}

/**
 * @brief Verify wrapped feedback triangles preserve area and valid UVs.
 */
static void wrapping(void)
{
    FeedbackMesh mesh = { 0 };

    mesh.wrap = 1;

    for (unsigned y = 0; y <= FEEDBACK_Y; ++y)
    {
        for (unsigned x = 0; x <= FEEDBACK_X; ++x)
        {
            mesh.uv[y * (FEEDBACK_X + 1) + x] = (FeedbackUV){ x * 3.1f / FEEDBACK_X - .6f, y * 2.3f / FEEDBACK_Y - .4f };
        }
    }

    Capture c = { 0 };

    feedback_emit(&mesh, triangle, &c);
    TEST_ASSERT_TRUE_MESSAGE(fabs(c.area - DISPLAY_WIDTH * DISPLAY_HEIGHT) < 2 && c.triangles > 640 && c.triangles < 3000, "fabs(c.area - DISPLAY_WIDTH * DISPLAY_HEIGHT) < 2 && c.triangles > 640 && c.triangles < 3000");
}

/**
 * @brief Verify reference waveform scaling and spatial smoothing.
 */
static void smoothing(void)
{
    MusicFeatures audio = { 0 };
    float         wave[2][MILK_AUDIO_SAMPLES];

    audio.milk_audio.waveform[0][0] = 128;

    for (unsigned i = 0; i < MILK_AUDIO_SAMPLES; ++i)
    {
        audio.milk_audio.waveform[1][i] = 256;
    }

    milk_smooth_wave(&audio, 2, .5f, wave);

    for (unsigned i = 0; i < MILK_AUDIO_SAMPLES; ++i)
    {
        TEST_ASSERT_TRUE_MESSAGE(fabsf(wave[0][i] - 2 * powf(.5f, i)) < .00001f, "fabsf(wave[0][i] - 2 * powf(.5f, i)) < .00001f");
        TEST_ASSERT_TRUE_MESSAGE(wave[1][i] == 4, "wave[1][i] == 4");
    }

    TEST_ASSERT_TRUE_MESSAGE(audio.milk_audio.waveform[0][1] == 0, "audio.milk_audio.waveform[0][1] == 0");    // Filter must not alter shared audio.
    milk_smooth_wave(&audio, 3, 0, wave);
    TEST_ASSERT_TRUE_MESSAGE(wave[0][0] == 3 && wave[0][1] == 0 && wave[1][40] == 6, "wave[0][0] == 3 && wave[0][1] == 0 && wave[1][40] == 6");
    milk_smooth_wave(&audio, 1, 1, wave);
    TEST_ASSERT_TRUE_MESSAGE(wave[0][127] == 1 && wave[1][127] == 2, "wave[0][127] == 1 && wave[1][127] == 2");
    memset(&audio, 0, sizeof(audio));
    milk_smooth_wave(&audio, 1, .9f, wave);
    TEST_ASSERT_TRUE_MESSAGE(wave[0][0] == 0 && wave[0][127] == 0, "wave[0][0] == 0 && wave[0][127] == 0");    // No temporal tail.
}

/**
 * @brief Verify echo parameter blending and texture orientation.
 */
static void echo_composition(void)
{
    MilkEcho echo = { 2, .5f, 0, 1 };

    for (unsigned orientation = 0; orientation < 4; ++orientation)
    {
        echo.orientation = orientation;

        float u, v;

        milk_echo_uv(&echo, 0, 0, &u, &v);
        TEST_ASSERT_TRUE_MESSAGE(u == (orientation & 1 ? .75f : .25f), "u == (orientation & 1 ? .75f : .25f)");
        TEST_ASSERT_TRUE_MESSAGE(v == (orientation & 2 ? .75f : .25f), "v == (orientation & 2 ? .75f : .25f)");

        for (unsigned wrap = 0; wrap < 2; ++wrap)
        {
            echo.wrap = wrap;

            const float zooms[] = { .2f, .9998f, 1, 7.113829f, 100 };

            for (unsigned i = 0; i < sizeof(zooms) / sizeof(zooms[0]); ++i)
            {
                echo.zoom = zooms[i];

                Capture capture = { 0 };

                feedback_echo_emit(&echo, triangle, &capture);
                TEST_ASSERT_TRUE_MESSAGE(fabs(capture.area - DISPLAY_WIDTH * DISPLAY_HEIGHT) < 10, "fabs(capture.area - DISPLAY_WIDTH * DISPLAY_HEIGHT) < 10");
                TEST_ASSERT_TRUE_MESSAGE(capture.triangles <= 150, "capture.triangles <= 150");
            }
        }

        echo.zoom = 2;
    }

    Preset a = { 0 }, b = { 0 };

    a.kind = b.kind              = (PresetKind)0;
    a.milk.frame[ML_ECHO_ALPHA]  = .5f;
    a.milk.frame[ML_ECHO_ZOOM]   = 2;
    b.milk.frame[ML_ECHO_ALPHA]  = .75f;
    b.milk.frame[ML_ECHO_ZOOM]   = 3;
    b.milk.frame[ML_ECHO_ORIENT] = 3;

    MilkComposite mix;

    milk_composite(&a, &b, .25f, &mix);
    TEST_ASSERT_TRUE_MESSAGE(mix.echo[0].alpha == .375f && mix.echo[1].alpha == .1875f, "mix.echo[0].alpha == .375f && mix.echo[1].alpha == .1875f");
    TEST_ASSERT_TRUE_MESSAGE(mix.base == .4375f && mix.echo[1].orientation == 3, "mix.base == .4375f && mix.echo[1].orientation == 3");
    milk_composite(&a, &b, 0, &mix);
    TEST_ASSERT_TRUE_MESSAGE(mix.base == .5f && mix.echo[1].alpha == 0, "mix.base == .5f && mix.echo[1].alpha == 0");
    milk_composite(&a, &b, 1, &mix);
    TEST_ASSERT_TRUE_MESSAGE(mix.base == .25f && mix.echo[0].alpha == 0, "mix.base == .25f && mix.echo[0].alpha == 0");
    milk_composite(NULL, NULL, .7f, &mix);
    TEST_ASSERT_TRUE_MESSAGE(mix.base == 1 && mix.echo[0].alpha == 0 && mix.echo[1].alpha == 0, "mix.base == 1 && mix.echo[0].alpha == 0 && mix.echo[1].alpha == 0");
}

/**
 * @brief Verify geometry, sample counts, and discontinuities of classic wave modes.
 */
static void waveform_modes(void)
{
    Preset p = { 0 };

    p.milk.frame[ML_WAVE_X] = p.milk.frame[ML_WAVE_Y] = .5f;

    MusicFeatures audio                       = { 0 };
    float         wave[2][MILK_AUDIO_SAMPLES] = { { 0 } };
    MilkWave      shape;

    for (unsigned mode = 0; mode <= 8; ++mode)
    {
        p.milk.frame[ML_WAVE_MODE] = mode;

        milk_wave_shape(&p, &audio, wave, &shape);
        TEST_ASSERT_TRUE_MESSAGE(shape.count > 1 && shape.count <= MILK_WAVE_MAX, "shape.count > 1 && shape.count <= MILK_WAVE_MAX");

        for (unsigned i = 0; i < shape.count; ++i)
        {
            TEST_ASSERT_TRUE_MESSAGE(isfinite(shape.point[i].x) && isfinite(shape.point[i].y), "isfinite(shape.point[i].x) && isfinite(shape.point[i].y)");
        }

        TEST_ASSERT_TRUE_MESSAGE(shape.closed == (mode == 0), "shape.closed == (mode == 0)");
    }

    /* Complex stereo mode: product terms, not an ordinary L/R line. */

    for (unsigned i = 0; i < MILK_AUDIO_SAMPLES; ++i)
    {
        wave[0][i] = .2f;
        wave[1][i] = .3f;
    }

    p.milk.frame[ML_WAVE_MODE] = 5;

    milk_wave_shape(&p, &audio, wave, &shape);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(shape.point[0].x - (MILK_VIEWPORT_HALF_WIDTH + .12f * MILK_VIEWPORT_HALF_HEIGHT)) < .001f, "fabsf(shape.point[0].x - (MILK_VIEWPORT_HALF_WIDTH + .12f * MILK_VIEWPORT_HALF_HEIGHT)) < .001f");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(shape.point[0].y - (MILK_VIEWPORT_HALF_HEIGHT - .05f * MILK_VIEWPORT_HALF_HEIGHT)) < .001f, "fabsf(shape.point[0].y - (MILK_VIEWPORT_HALF_HEIGHT - .05f * MILK_VIEWPORT_HALF_HEIGHT)) < .001f");
    /* Separate stereo lines: wave_y controls separation, no bridge. */
    memset(wave, 0, sizeof(wave));

    p.milk.frame[ML_WAVE_MODE] = 7;

    milk_wave_shape(&p, &audio, wave, &shape);
    TEST_ASSERT_TRUE_MESSAGE(shape.count == 426 && shape.break_at == 213, "shape.count == 426 && shape.break_at == 213");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(shape.point[20].y - (DISPLAY_HEIGHT * .375f)) < .01f, "fabsf(shape.point[20].y - (DISPLAY_HEIGHT * .375f)) < .01f");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(shape.point[233].y - (DISPLAY_HEIGHT * .625f)) < .01f, "fabsf(shape.point[233].y - (DISPLAY_HEIGHT * .625f)) < .01f");

    p.milk.frame[ML_WAVE_MYSTERY] = 1;

    milk_wave_shape(&p, &audio, wave, &shape);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(shape.point[0].y - shape.point[212].y) > DISPLAY_HEIGHT * .95f, "fabsf(shape.point[0].y - shape.point[212].y) > DISPLAY_HEIGHT * .95f");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(shape.point[0].x - shape.point[212].x) < 1, "fabsf(shape.point[0].x - shape.point[212].x) < 1");
    /* A single spectral peak moves only that vertex; zero is finite. */
    p.milk.frame[ML_WAVE_MODE]    = 8;
    p.milk.frame[ML_WAVE_MYSTERY] = 0;

    for (unsigned i = 0; i < MILK_SPECTRUM_POINTS; ++i)
    {
        audio.spectrum_left[i] = 1;
    }

    audio.spectrum_left[40] = expf(-1);

    milk_wave_shape(&p, &audio, wave, &shape);
    TEST_ASSERT_TRUE_MESSAGE(shape.count == 256 && fabsf(shape.point[39].y - MILK_VIEWPORT_HALF_HEIGHT) < .001f, "shape.count == 256 && fabsf(shape.point[39].y - MILK_VIEWPORT_HALF_HEIGHT) < .001f");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(shape.point[40].y - DISPLAY_HEIGHT * .55f) < .001f, "fabsf(shape.point[40].y - DISPLAY_HEIGHT * .55f) < .001f");
    /* Script mode's momentum must preserve a straight silent line. */
    p.milk.frame[ML_WAVE_MODE] = 4;

    milk_wave_shape(&p, &audio, wave, &shape);

    for (unsigned i = 0; i < shape.count; ++i)
    {
        TEST_ASSERT_TRUE_MESSAGE(fabsf(shape.point[i].y - MILK_VIEWPORT_HALF_HEIGHT) < .001f, "fabsf(shape.point[i].y - MILK_VIEWPORT_HALF_HEIGHT) < .001f");
        TEST_ASSERT_TRUE_MESSAGE(fabsf(shape.point[i].x - 640.0f * i / shape.count) < .02f, "fabsf(shape.point[i].x - 640.0f * i / shape.count) < .02f");
    }
}

/**
 * @brief Verify deterministic corner colors and presentation-only shading.
 */
static void shading(void)
{
    Preset a = { 0 }, b = { 0 };

    a.kind = b.kind = (PresetKind)0;
    a.seed          = 17;
    b.seed          = 93;

    MilkComposite white, first, second, mixed;

    milk_composite(&a, NULL, 0, &white);
    TEST_ASSERT_TRUE_MESSAGE(!white.shaded, "!white.shaded");

    for (unsigned i = 0; i < 4; ++i)
    {
        for (unsigned c = 0; c < 3; ++c)
        {
            TEST_ASSERT_TRUE_MESSAGE(white.shade[i][c] == 1, "white.shade[i][c] == 1");
        }
    }

    a.milk.frame[ML_SHADER] = b.milk.frame[ML_SHADER] = 1;

    milk_composite(&a, NULL, 0, &first);
    milk_composite(&b, NULL, 0, &second);
    TEST_ASSERT_TRUE_MESSAGE(first.shaded && memcmp(first.shade, second.shade, sizeof(first.shade)) != 0, "first.shaded && memcmp(first.shade, second.shade, sizeof(first.shade))");
    milk_composite(&a, &b, .25f, &mixed);

    for (unsigned i = 0; i < 4; ++i)
    {
        float peak = 0;

        for (unsigned c = 0; c < 3; ++c)
        {
            TEST_ASSERT_TRUE_MESSAGE(first.shade[i][c] >= .5f && first.shade[i][c] <= 1, "first.shade[i][c] >= .5f && first.shade[i][c] <= 1");

            peak = fmaxf(peak, first.shade[i][c]);

            TEST_ASSERT_TRUE_MESSAGE(fabsf(mixed.shade[i][c] - (.75f * first.shade[i][c] + .25f * second.shade[i][c])) < 1e-6f, "fabsf(mixed.shade[i][c] - (.75f * first.shade[i][c] + .25f * second.shade[i][c])) < 1e-6f");
        }

        TEST_ASSERT_TRUE_MESSAGE(peak == 1, "peak == 1");

        float rgb[3];

        milk_shade_at(&first, i & 1, i >> 1, rgb);

        for (unsigned c = 0; c < 3; ++c)
        {
            TEST_ASSERT_TRUE_MESSAGE(fabsf(rgb[c] - first.shade[i][c]) < 1e-6f, "fabsf(rgb[c] - first.shade[i][c]) < 1e-6f");
        }
    }

    float center[3];

    milk_shade_at(&first, .5f, .5f, center);

    for (unsigned c = 0; c < 3; ++c)
    {
        TEST_ASSERT_TRUE_MESSAGE(fabsf(center[c] - .5f * (first.shade[1][c] + first.shade[2][c])) < 1e-6f, "fabsf(center[c] - .5f * (first.shade[1][c] + first.shade[2][c])) < 1e-6f");
    }

    a.milk.inputs[0] = 10;

    milk_composite(&a, NULL, 0, &second);
    TEST_ASSERT_TRUE_MESSAGE(memcmp(first.shade, second.shade, sizeof(first.shade)) != 0, "memcmp(first.shade, second.shade, sizeof(first.shade))");
}

/**
 * @brief Latest motion-vector endpoints and total captured segments.
 */
typedef struct
{
    unsigned     count; /**< Number of line callbacks. */
    PresetVertex first; /**< Start of the latest captured segment. */
    PresetVertex last;  /**< End of the latest captured segment. */
} MotionCapture;

/**
 * @brief Capture a validated motion-vector segment.
 *
 * @param context MotionCapture destination.
 * @param a Start vertex.
 * @param b End vertex.
 * @param alpha Opacity to validate.
 */
static void motion_line(void* context, PresetVertex a, PresetVertex b, float alpha)
{
    MotionCapture* capture = context;

    check_vertex(a, alpha);
    check_vertex(b, alpha);

    capture->first = a;
    capture->last  = b;

    ++capture->count;
}

/**
 * @brief Verify motion-vector settings, clipping, and drawing behavior.
 */
static void motion_effects(void)
{
    Preset a = { 0 }, b = { 0 };

    a.kind = b.kind       = (PresetKind)0;
    a.milk.frame[ML_MV_X] = a.milk.frame[ML_MV_Y] = 3;
    a.milk.frame[ML_MV_A]                         = 1;
    a.milk.frame[ML_MV_L]                         = 2;
    a.milk.frame[ML_MV_R]                         = .5f;

    MilkMotion motion;

    TEST_ASSERT_TRUE_MESSAGE(milk_motion_parameters(&a, NULL, 0, &motion), "milk_motion_parameters(&a, NULL, 0, &motion)");

    FeedbackMesh mesh = { 0 };

    for (unsigned y = 0; y <= FEEDBACK_Y; ++y)
    {
        for (unsigned x = 0; x <= FEEDBACK_X; ++x)
        {
            mesh.uv[y * (FEEDBACK_X + 1) + x] = (FeedbackUV){ (float)x / FEEDBACK_X + .02f, (float)y / FEEDBACK_Y + .03f };
        }
    }

    MotionCapture capture = { 0 };
    PresetCanvas  canvas  = { &capture, NULL, motion_line, NULL, NULL, 1, NULL, NULL, NULL, NULL };
    Preset        before  = a;

    milk_motion_draw(&motion, &mesh, &canvas);
    TEST_ASSERT_TRUE_MESSAGE(!memcmp(&before, &a, sizeof(a)), "!memcmp(&before, &a, sizeof(a))");
    TEST_ASSERT_TRUE_MESSAGE(capture.count == 4, "capture.count == 4");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(capture.last.x - capture.first.x - 25.6f) < .001f, "fabsf(capture.last.x - capture.first.x - 25.6f) < .001f");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(capture.last.y - capture.first.y - DISPLAY_HEIGHT * .06f) < .001f, "fabsf(capture.last.y - capture.first.y - DISPLAY_HEIGHT * .06f) < .001f");
    TEST_ASSERT_TRUE_MESSAGE(capture.first.r == 127.5f && capture.first.g == 0, "capture.first.r == 127.5f && capture.first.g == 0");

    motion.length = 0;
    capture.count = 0;

    milk_motion_draw(&motion, &mesh, &canvas);
    TEST_ASSERT_TRUE_MESSAGE(capture.count == 4, "capture.count == 4");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(capture.last.x - capture.first.x - 1) < .001f, "fabsf(capture.last.x - capture.first.x - 1) < .001f");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(capture.last.y - capture.first.y + MILK_VIEWPORT_ASPECT) < .001f, "fabsf(capture.last.y - capture.first.y + MILK_VIEWPORT_ASPECT) < .001f");
    /* Reference fractional-grid positioning and one-vector offset. */
    motion.x = motion.y = 1.5f;
    capture.count       = 0;

    milk_motion_draw(&motion, &mesh, &canvas);
    TEST_ASSERT_TRUE_MESSAGE(capture.count == 1 && fabsf(capture.first.x - 640.0f / 3) < .001f, "capture.count == 1 && fabsf(capture.first.x - 640.0f / 3) < .001f");

    motion.x = motion.y = 1;
    motion.dx           = -.5f;
    motion.dy           = .5f;
    capture.count       = 0;

    milk_motion_draw(&motion, &mesh, &canvas);
    TEST_ASSERT_TRUE_MESSAGE(capture.count == 1 && capture.first.x == 320 && capture.first.y == MILK_VIEWPORT_HALF_HEIGHT, "capture.count == 1 && capture.first.x == 320 && capture.first.y == MILK_VIEWPORT_HALF_HEIGHT");
    /* Dynamic equations cannot grow the draw loop beyond the reference cap. */
    a.milk.frame[ML_MV_X] = a.milk.frame[ML_MV_Y] = 1e30f;
    a.milk.frame[ML_MV_L]                         = 1e30f;

    TEST_ASSERT_TRUE_MESSAGE(milk_motion_parameters(&a, NULL, 0, &motion), "milk_motion_parameters(&a, NULL, 0, &motion)");
    TEST_ASSERT_TRUE_MESSAGE(motion.x == MILK_MOTION_X && motion.y == MILK_MOTION_Y && motion.length == 100, "motion.x == MILK_MOTION_X && motion.y == MILK_MOTION_Y && motion.length == 100");

    capture.count = 0;

    milk_motion_draw(&motion, &mesh, &canvas);
    TEST_ASSERT_TRUE_MESSAGE(capture.count > 2000 && capture.count <= MILK_MOTION_X * MILK_MOTION_Y, "capture.count > 2000 && capture.count <= MILK_MOTION_X * MILK_MOTION_Y");

    b.milk.frame[ML_MV_X] = 4;
    b.milk.frame[ML_MV_Y] = 2;
    b.milk.frame[ML_MV_A] = .5f;

    TEST_ASSERT_TRUE_MESSAGE(milk_motion_parameters(&a, &b, .5f, &motion), "milk_motion_parameters(&a, &b, .5f, &motion)");
    TEST_ASSERT_TRUE_MESSAGE(motion.x == 34 && motion.y == 25 && motion.alpha == .75f, "motion.x == 34 && motion.y == 25 && motion.alpha == .75f");

    a.milk.frame[ML_MV_A] = 0;

    TEST_ASSERT_TRUE_MESSAGE(!milk_motion_parameters(&a, NULL, 0, &motion), "!milk_motion_parameters(&a, NULL, 0, &motion)");
    TEST_ASSERT_TRUE_MESSAGE(!milk_motion_parameters(NULL, NULL, 0, &motion), "!milk_motion_parameters(NULL, NULL, 0, &motion)");
}

/**
 * @brief Validate and count center-darkening callbacks.
 *
 * @param context Unused callback context.
 * @param opacity Opacity to validate.
 */
static void center_draw(void* context, float opacity)
{
    (void)context;
    TEST_ASSERT_TRUE_MESSAGE(opacity > 0 && opacity <= 1, "opacity > 0 && opacity <= 1");
    ++centers;
}

/**
 * @brief Verify presentation gain, inversion, and center darkening.
 */
static void presentation_gain(void)
{
    Preset a, b;

    preset_init(&a, (PresetKind)0, 7);

    b                         = a;
    a.milk.frame[ML_GAMMA]    = 2;
    b.milk.frame[ML_GAMMA]    = 6;
    a.milk.frame[ML_INVERT]   = 0;
    b.milk.frame[ML_INVERT]   = 1;
    a.milk.frame[ML_DARKEN]   = 0;
    b.milk.frame[ML_DARKEN]   = 1;
    b.milk.frame[ML_BRIGHTEN] = 1;
    b.milk.frame[ML_SOLARIZE] = 1;

    MilkComposite c;

    milk_composite(&a, &b, .25f, &c);
    TEST_ASSERT_TRUE_MESSAGE(c.gamma == 3 && !c.invert && !c.darken && !c.brighten && !c.solarize, "c.gamma == 3 && !c.invert");
    milk_composite(&a, &b, .75f, &c);
    TEST_ASSERT_TRUE_MESSAGE(c.gamma == 5 && c.invert && c.darken && c.brighten && c.solarize, "c.gamma == 5 && c.invert");

    a.milk.frame[ML_GAMMA] = 99;

    milk_composite(&a, NULL, 0, &c);
    TEST_ASSERT_TRUE_MESSAGE(c.gamma == 8, "c.gamma == 8");

    a.milk.frame[ML_GAMMA] = -2;

    milk_composite(&a, NULL, 0, &c);
    TEST_ASSERT_TRUE_MESSAGE(c.gamma == 0, "c.gamma == 0");
    TEST_ASSERT_TRUE_MESSAGE(a.milk.frame[ML_GAMMA] == -2, "a.milk.frame[ML_GAMMA] == -2");
    milk_composite(NULL, NULL, 0, &c);
    TEST_ASSERT_TRUE_MESSAGE(c.gamma == 1 && !c.invert, "c.gamma == 1 && !c.invert");
}

/**
 * @brief Compare the optimized power helper against libm and edge cases.
 */
static void power_specialization(void)
{
    float worst = 0;

    for (unsigned i = 0; i <= 200000; ++i)
    {
        float x        = exp2f(-19.93156857f + i * (39.86313714f / 200000));
        float expected = powf(x, 2.5f);
        float relative = fabsf(milk_pow(x, 2.5f) - expected) / expected;

        worst = fmaxf(worst, relative);

        TEST_ASSERT_TRUE_MESSAGE(relative < 5e-7f, "relative < 5e-7f");
    }

    const float bases[]  = { -2, -0.0f, 0, 1e-30f, 1e-6f, 1, 1e6f, 1e20f, NAN, INFINITY };
    const float powers[] = { -1, 0, .5f, 2, 2.5f, 3, NAN };

    for (unsigned i = 0; i < sizeof(bases) / sizeof(*bases); ++i)
    {
        for (unsigned j = 0; j < sizeof(powers) / sizeof(*powers); ++j)
        {
            float x = bases[i], y = powers[j];

            if (y == 2.5f && x >= 1e-6f && x <= 1e6f)
            {
                continue;
            }

            float expected = milk_finite(powf(x, y)), actual = milk_pow(x, y);

            TEST_ASSERT_TRUE_MESSAGE(actual == expected, "actual == expected");

            if (actual == 0)
            {
                TEST_ASSERT_TRUE_MESSAGE(!!signbit(actual) == !!signbit(expected), "!!signbit(actual) == !!signbit(expected)");
            }
        }
    }

    printf("Power 2.5 maximum sampled relative error: %.9g\n", (double)worst);
}

/**
 * @brief Run regression checks for MilkDrop rendering and math.
 *
 */
static void milk_regressions(void)
{
    uint32_t pair_bits = 123;

    for (unsigned i = 0; i < 2000000; ++i)
    {
        pair_bits = pair_bits * 1664525u + 1013904223u;

        float x, ps, pc;

        memcpy(&x, &pair_bits, sizeof(x));
        milk_fast_sincos(x, &ps, &pc);

        float ss = milk_fast_sin(x), sc = milk_fast_cos(x);

        TEST_ASSERT_TRUE_MESSAGE(!memcmp(&ps, &ss, sizeof(float)) || (isnan(ps) && isnan(ss)), "!memcmp(&ps, &ss, sizeof(float)) || (isnan(ps) && isnan(ss))");
        TEST_ASSERT_TRUE_MESSAGE(!memcmp(&pc, &sc, sizeof(float)) || (isnan(pc) && isnan(sc)), "!memcmp(&pc, &sc, sizeof(float)) || (isnan(pc) && isnan(sc))");
    }

    const float pair_edges[] = { -0.0f, 0, -8192, 8192, -1048576, 1048576, -INFINITY, INFINITY, NAN };

    for (unsigned i = 0; i < sizeof(pair_edges) / sizeof(pair_edges[0]); ++i)
    {
        float ps, pc, ss = milk_fast_sin(pair_edges[i]), sc = milk_fast_cos(pair_edges[i]);

        milk_fast_sincos(pair_edges[i], &ps, &pc);
        TEST_ASSERT_TRUE_MESSAGE(!memcmp(&ps, &ss, sizeof(float)) || (isnan(ps) && isnan(ss)), "!memcmp(&ps, &ss, sizeof(float)) || (isnan(ps) && isnan(ss))");
        TEST_ASSERT_TRUE_MESSAGE(!memcmp(&pc, &sc, sizeof(float)) || (isnan(pc) && isnan(sc)), "!memcmp(&pc, &sc, sizeof(float)) || (isnan(pc) && isnan(sc))");
    }

    float    worst_trig_error = 0;
    uint32_t angles           = 123;

    for (unsigned i = 0; i < 1000000; ++i)
    {
        angles = angles * 1664525u + 1013904223u;

        float angle = ((int32_t)angles / 2147483648.0f) * 8192;
        float e     = fmaxf(fabsf(milk_fast_sin(angle) - sinf(angle)), fabsf(milk_fast_cos(angle) - cosf(angle)));

        worst_trig_error = fmaxf(worst_trig_error, e);

        TEST_ASSERT_TRUE_MESSAGE(e < .000002f, "e < .000002f");
    }

    /* Exercise the wider reducer against double-precision reference trig. */

    for (unsigned i = 0; i < 1000000; ++i)
    {
        angles = angles * 1664525u + 1013904223u;

        float x = ((int32_t)angles / 2147483648.0f) * 1048576;

        TEST_ASSERT_TRUE_MESSAGE(fabs((double)milk_fast_sin(x) - sin((double)x)) < .000002, "fabs((double)milk_fast_sin(x) - sin((double)x)) < .000002");
        TEST_ASSERT_TRUE_MESSAGE(fabs((double)milk_fast_cos(x) - cos((double)x)) < .000002, "fabs((double)milk_fast_cos(x) - cos((double)x)) < .000002");
    }

    for (int turn = -166886; turn <= 166886; ++turn)
    {
        float       x           = (float)(turn * 6.2831853071795864769);
        const float neighbors[] = { nextafterf(x, -INFINITY), x, nextafterf(x, INFINITY) };

        for (unsigned i = 0; i < 3; ++i)
        {
            TEST_ASSERT_TRUE_MESSAGE(fabs((double)milk_fast_sin(neighbors[i]) - sin((double)neighbors[i])) < .000002, "fabs((double)milk_fast_sin(neighbors[i]) - sin((double)neighbors[i])) < .000002");
            TEST_ASSERT_TRUE_MESSAGE(fabs((double)milk_fast_cos(neighbors[i]) - cos((double)neighbors[i])) < .000002, "fabs((double)milk_fast_cos(neighbors[i]) - cos((double)neighbors[i])) < .000002");
        }
    }

    const float boundaries[] = { -1048576.125f, -1048576, -1048575.9375f, 1048575.9375f, 1048576, 1048576.125f, -8192.001f, -8192, -3.14159265f, -1.5707963f, -0.0f, 0, 1.5707963f, 3.14159265f, 8192, 8192.001f, 1e20f };

    for (unsigned i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i)
    {
        float x = boundaries[i];

        TEST_ASSERT_TRUE_MESSAGE(fabsf(milk_fast_sin(x) - sinf(x)) < .000002f, "fabsf(milk_fast_sin(x) - sinf(x)) < .000002f");
        TEST_ASSERT_TRUE_MESSAGE(fabsf(milk_fast_cos(x) - cosf(x)) < .000002f, "fabsf(milk_fast_cos(x) - cosf(x)) < .000002f");
    }

    TEST_ASSERT_TRUE_MESSAGE(isnan(milk_fast_sin(NAN)) && isnan(milk_fast_cos(INFINITY)), "isnan(milk_fast_sin(NAN)) && isnan(milk_fast_cos(INFINITY))");
    printf("Fast trig maximum sampled absolute error: %.9g\n", (double)worst_trig_error);
}

/**
 * @brief Preserve IEEE behavior of inline equation helpers.
 */
static void inline_math(void)
{
    volatile float edge[] = { NAN, INFINITY, -INFINITY, -0.0f, 0.0f, 3.5f, -2.0f };

    TEST_ASSERT_TRUE_MESSAGE(milk_finite(edge[0]) == 0 && milk_finite(edge[1]) == 0 && milk_finite(edge[2]) == 0, "milk_finite(edge[0]) == 0 && milk_finite(edge[1]) == 0 && milk_finite(edge[2]) == 0");
    TEST_ASSERT_TRUE_MESSAGE(signbit(milk_finite(edge[3])) && milk_finite(edge[5]) == 3.5f, "signbit(milk_finite(edge[3])) && milk_finite(edge[5]) == 3.5f");
    TEST_ASSERT_TRUE_MESSAGE(milk_div(edge[5], edge[3]) == 0 && milk_div(edge[5], edge[4]) == 0, "milk_div(edge[5], edge[3]) == 0 && milk_div(edge[5], edge[4]) == 0");
    TEST_ASSERT_TRUE_MESSAGE(milk_div(edge[0], edge[5]) == 0 && milk_div(edge[1], edge[5]) == 0, "milk_div(edge[0], edge[5]) == 0 && milk_div(edge[1], edge[5]) == 0");
    TEST_ASSERT_TRUE_MESSAGE(milk_div(edge[5], edge[6]) == -1.75f, "milk_div(edge[5], edge[6]) == -1.75f");
    TEST_ASSERT_TRUE_MESSAGE(milk_sign(edge[0]) == 0 && milk_sign(edge[3]) == 0, "milk_sign(edge[0]) == 0 && milk_sign(edge[3]) == 0");
    TEST_ASSERT_TRUE_MESSAGE(milk_sign(edge[1]) == 1 && milk_sign(edge[2]) == -1, "milk_sign(edge[1]) == 1 && milk_sign(edge[2]) == -1");
    TEST_ASSERT_TRUE_MESSAGE(!milk_equal(edge[0], edge[0]) && !milk_equal(edge[1], edge[1]), "!milk_equal(edge[0], edge[0]) && !milk_equal(edge[1], edge[1])");
    TEST_ASSERT_TRUE_MESSAGE(milk_equal(.000009f, 0) && !milk_equal(.00001f, 0), "milk_equal(.000009f, 0) && !milk_equal(.00001f, 0)");
    TEST_ASSERT_TRUE_MESSAGE(milk_div(1, 0) == 0 && milk_mod(-7, 3) == 1 && milk_equal(.000001f, 0), "milk_div(1, 0) == 0 && milk_mod(-7, 3) == 1 && milk_equal(.000001f, 0)");
    TEST_ASSERT_TRUE_MESSAGE(!milk_truth(.000001f) && milk_truth(.1f), "!milk_truth(.000001f) && milk_truth(.1f)");

    volatile float dividend[] = { -7.9f, 17.9f, 0.0f, 4294967296.0f, INFINITY, NAN };

    TEST_ASSERT_EQUAL_FLOAT(1, milk_mod(dividend[0], -3.9f));
    TEST_ASSERT_EQUAL_FLOAT(1, milk_mod(dividend[1], 8));
    TEST_ASSERT_EQUAL_FLOAT(0, milk_mod(dividend[0], .9f));
    TEST_ASSERT_EQUAL_FLOAT(0, milk_mod(dividend[0], 0));
    TEST_ASSERT_EQUAL_FLOAT(0, milk_mod(dividend[2], 8));
    TEST_ASSERT_EQUAL_FLOAT(0, milk_mod(dividend[3], 8));
    TEST_ASSERT_EQUAL_FLOAT(0, milk_mod(dividend[4], 8));
    TEST_ASSERT_EQUAL_FLOAT(0, milk_mod(dividend[5], 8));
    TEST_ASSERT_FALSE(milk_truth(NAN));
    TEST_ASSERT_TRUE(milk_truth(INFINITY));
    TEST_ASSERT_FALSE(milk_truth(.00001f));
    TEST_ASSERT_TRUE(milk_truth(nextafterf(.00001f, INFINITY)));
    TEST_ASSERT_TRUE_MESSAGE(milk_sqrt(-9) == 3 && milk_pow(-1, .5f) == 0, "milk_sqrt(-9) == 3 && milk_pow(-1, .5f) == 0");
    TEST_ASSERT_TRUE_MESSAGE(milk_bitand(-3, 6) == 4 && milk_bitor(5, 2) == 7 && milk_bitand(3.9f, 2) == 2, "milk_bitand(-3, 6) == 4 && milk_bitor(5, 2) == 7 && milk_bitand(3.9f, 2) == 2");
}

/**
 * @brief Verify deterministic rendering across the entire preset library.
 */
static void preset_rendering(void)
{
    unsigned maximum = 0;

    for (unsigned kind = 0; kind < MILK_PRESET_COUNT; ++kind)
    {
        Preset a, b;

        preset_init(&a, (PresetKind)kind, 71);

        b = a;

        MusicFeatures music;

        music_init(&music);

        FeedbackMesh mesh;

        music.relative[0]   = 1.2f;
        music.relative[1]   = .9f;
        music.relative[2]   = .7f;
        music.attenuated[0] = 1;
        music.attenuated[1] = .8f;
        music.attenuated[2] = .6f;

        for (unsigned tick = 0; tick < 150; ++tick)
        {
            if (tick == 120)
            {
                memset(&music, 0, sizeof(music));
            }

            music.time  = (tick + 1) * .02f;
            music.frame = tick + 1;

            for (unsigned i = 0; i < MILK_AUDIO_SAMPLES; ++i)
            {
                for (unsigned channel = 0; channel < 2; ++channel)
                {
                    music.milk_audio.waveform[channel][i] = tick < 120 ? 64.0f * sinf(i * .17f + tick * .08f + channel) : 0;
                }
            }

            for (unsigned i = 0; i < MILK_SPECTRUM_POINTS; ++i)
            {
                music.spectrum_left[i] = tick < 120 ? .3f * expf(-i / 40.0f) : 0;
            }

            for (unsigned ch = 0; ch < 2; ++ch)
            {
                for (unsigned i = 0; i < MILK_AUDIO_BINS; ++i)
                {
                    music.custom_wave[ch][i]         = tick < 120 ? .25f * sinf(i * .06f + tick * .08f + ch) : 0;
                    music.milk_audio.spectrum[ch][i] = tick < 120 ? .3f * expf(-i / 40.0f) : 0;
                }
            }

            preset_step(&a, &music, .02f);
            preset_step(&b, &music, .02f);
            feedback_build(&a, NULL, 0, &mesh);

            FeedbackMesh other;

            feedback_build(&b, NULL, 0, &other);
            TEST_ASSERT_TRUE_MESSAGE(!memcmp(&a, &b, sizeof(a)) && !memcmp(&mesh, &other, sizeof(mesh)), "!memcmp(&a, &b, sizeof(a)) && !memcmp(&mesh, &other, sizeof(mesh))");

            draw_calls = 0;

            PresetCanvas canvas = { NULL, draw_triangle, draw_line, draw_sprite, NULL, 1, center_draw, object_triangle, object_line, NULL };

            centers = 0;

            preset_draw(&a, &canvas, &music);
            TEST_ASSERT_TRUE_MESSAGE(centers == (a.milk.frame[ML_DARKEN_CENTER] != 0), "centers == (a.milk.frame[ML_DARKEN_CENTER] != 0)");
            TEST_ASSERT_TRUE_MESSAGE(draw_calls < 65536, "draw_calls < 65536");
            TEST_ASSERT_TRUE_MESSAGE(!memcmp(&a, &b, sizeof(a)), "!memcmp(&a, &b, sizeof(a))");

            MilkMotion motion;

            if (milk_motion_parameters(&a, &b, .3f, &motion))
            {
                Preset before = a;

                draw_calls = 0;

                milk_motion_draw(&motion, &mesh, &canvas);
                TEST_ASSERT_TRUE_MESSAGE(draw_calls <= MILK_MOTION_X * MILK_MOTION_Y, "draw_calls <= MILK_MOTION_X * MILK_MOTION_Y");
                TEST_ASSERT_TRUE_MESSAGE(!memcmp(&a, &before, sizeof(a)), "!memcmp(&a, &before, sizeof(a))");
            }

            MilkComposite composite;

            milk_composite(&a, NULL, 0, &composite);
            TEST_ASSERT_TRUE_MESSAGE(isfinite(composite.gamma) && composite.gamma >= 0 && composite.gamma <= 8, "isfinite(composite.gamma) && composite.gamma >= 0 && composite.gamma <= 8");
            TEST_ASSERT_TRUE_MESSAGE(isfinite(composite.base) && composite.base >= 0 && composite.base <= 1, "isfinite(composite.base) && composite.base >= 0 && composite.base <= 1");

            Capture c = { 0 };

            feedback_emit(&mesh, triangle, &c);
            TEST_ASSERT_TRUE_MESSAGE(fabs(c.area - DISPLAY_WIDTH * DISPLAY_HEIGHT) < 10 && c.triangles < 6000, "fabs(c.area - DISPLAY_WIDTH * DISPLAY_HEIGHT) < 10 && c.triangles < 6000");

            if (c.triangles > maximum)
            {
                maximum = c.triangles;
            }

            for (unsigned i = 0; i < MILK_VARIABLES; ++i)
            {
                TEST_ASSERT_TRUE_MESSAGE(isfinite(a.milk.frame[i]) && isfinite(a.milk.vertex[i]), "isfinite(a.milk.frame[i]) && isfinite(a.milk.vertex[i])");
            }

            if (strstr(preset_name(a.kind), "Kevlar"))
            {
                float expected = .5f + .25f * sinf(1.4f * music.time) + .25f * sinf(2.25f * music.time);

                TEST_ASSERT_TRUE_MESSAGE(fabsf(a.milk.frame[ML_WAVE_R] - expected) < .00001f, "fabsf(a.milk.frame[ML_WAVE_R] - expected) < .00001f");
                TEST_ASSERT_TRUE_MESSAGE(fabsf(a.milk.vertex[ML_ROT] - (tick < 120 ? sinf(.2f) : 0)) < .00001f, "fabsf(a.milk.vertex[ML_ROT] - (tick < 120 ? sinf(.2f) : 0)) < .00001f");
            }
        }
    }

    printf("PASS: %u compiled presets, native equations, persistent scopes, silence/numerical guards, complete wrapped coverage; max %u textured triangles\n", MILK_PRESET_COUNT, maximum);
}

/**
 * @brief Fixtures are initialized by each scenario.
 */
void setUp(void)
{
    draw_calls = centers = 0;
}

/**
 * @brief This suite owns no external resources.
 */
void tearDown(void)
{}

/**
 * @brief Run the regression scenario.
 * @return Number of failed Unity cases.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(power_specialization);
    RUN_TEST(milk_regressions);
    RUN_TEST(inline_math);
    RUN_TEST(wrapping);
    RUN_TEST(smoothing);
    RUN_TEST(echo_composition);
    RUN_TEST(waveform_modes);
    RUN_TEST(shading);
    RUN_TEST(motion_effects);
    RUN_TEST(presentation_gain);
    RUN_TEST(preset_rendering);

    return UNITY_END();
}
