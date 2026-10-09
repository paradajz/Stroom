/* A small reference-semantics fixture replaces the generated program table in
 * this executable, so scope behavior is tested independently of pack contents. */
#include "milkdrop/preset.h"
#include "milkdrop/milk_wave_points.h"
#include "unity.h"
#include "milkdrop/viewport.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void nothing(float* v, uint32_t* rng);
static void shape_init(float* v, uint32_t* rng);
static void shape_frame(float* v, uint32_t* rng);
static void wave_init(float* v, uint32_t* rng);
static void wave_frame(float* v, uint32_t* rng);
static void wave_point(float* v, uint32_t* rng);

MILK_WAVE_POINT_LOOP(wave_points, wave_point(v, rng);)

static const MilkObjectProgram objects[] = {
    { .type = 0, .defaults = { [MO_INSTANCES] = 2, [MO_SIDES] = 4, [MO_Y] = .5f, [MO_RAD] = .2f, [MO_A] = 1, [MO_A2] = 1, [MO_TEX_ZOOM] = 1 }, .init = shape_init, .frame = shape_frame },
    { .type = 1, .defaults = { [MO_SAMPLES] = 4, [MO_SCALING] = 1, [MO_A] = .75f }, .init = wave_init, .frame = wave_frame, .points = wave_points },
};
const MilkProgram milk_programs[] = {
    { .name = "Object scope fixture", .defaults = { [ML_WAVE_SCALE] = 1, [ML_GAMMA] = 1 }, .init = nothing, .frame = nothing, .vertex = nothing, .object_count = 2, .objects = objects },
};
static unsigned   triangles, lines;
static double     area;
static MilkVertex first, last;

/**
 * @brief Provide a no-op compiled-equation callback for object tests.
 *
 * @param v Unused variable pool.
 * @param rng Unused RNG state.
 */
static void nothing(float* v, uint32_t* rng)
{
    (void)v;
    (void)rng;
}

/**
 * @brief Seed the shape fixture t variable.
 *
 * @param v Object variable pool.
 * @param rng Unused RNG state.
 */
static void shape_init(float* v, uint32_t* rng)
{
    (void)rng;

    v[MO_T1] = .25f;
}

/**
 * @brief Exercise instance inputs and persistent shape variables.
 *
 * @param v Object variable pool.
 * @param rng Unused RNG state.
 */
static void shape_frame(float* v, uint32_t* rng)
{
    (void)rng;

    v[MILK_OBJECT_BUILTINS] += 1;
    v[MO_X] = .25f + .5f * v[MO_INSTANCE];
    v[MO_R] = v[MO_T1];
    v[MO_T1] += 1;
    v[MO_G] = v[MO_Q1] / 42;
}

/**
 * @brief Seed the wave fixture t variable.
 *
 * @param v Object variable pool.
 * @param rng Unused RNG state.
 */
static void wave_init(float* v, uint32_t* rng)
{
    (void)rng;

    v[MO_T1] = .1f;
}

/**
 * @brief Set fixture frame variables to test transfer into wave point equations.
 *
 * @param v Object variable pool.
 * @param rng Unused RNG state.
 */
static void wave_frame(float* v, uint32_t* rng)
{
    (void)rng;

    v[MO_TIME] = 99;
    v[MO_Q2]   = 42;
    v[MO_T2]   = 3;
}

/**
 * @brief Evaluate fixture wave positions while exercising q/t persistence.
 *
 * @param v Object variable pool.
 * @param rng Unused RNG state.
 */
static void wave_point(float* v, uint32_t* rng)
{
    (void)rng;

    v[MILK_OBJECT_BUILTINS] += 1;
    v[MO_X] = v[MO_SAMPLE];
    v[MO_Y] = .5f + v[MO_VALUE1] + v[MO_T1];
    v[MO_R] = v[MO_Q2] / 42;
    v[MO_T2] += 1;
}

/**
 * @brief Assert custom vertex finiteness, viewport bounds, and opacity.
 *
 * @param v Vertex to validate.
 */
static void vertex(MilkVertex v)
{
    TEST_ASSERT_TRUE_MESSAGE(isfinite(v.x) && isfinite(v.y) && isfinite(v.r) && isfinite(v.g) && isfinite(v.b) && isfinite(v.a), "isfinite(v.x) && isfinite(v.y) && isfinite(v.r) && isfinite(v.g) && isfinite(v.b) && isfinite(v.a)");
    TEST_ASSERT_TRUE_MESSAGE(v.x >= -.01f && v.x <= (DISPLAY_WIDTH + .01f) && v.y >= -.01f && v.y <= (DISPLAY_HEIGHT + .01f), "v.x >= -.01f && v.x <= (DISPLAY_WIDTH + .01f) && v.y >= -.01f && v.y <= (DISPLAY_HEIGHT + .01f)");
    TEST_ASSERT_TRUE_MESSAGE(v.a >= 0 && v.a <= 1, "v.a >= 0 && v.a <= 1");
}

/**
 * @brief Validate and count a custom triangle, accumulating its area.
 *
 * @param ctx Unused callback context.
 * @param v Three vertices to validate.
 * @param textured Nonzero to also validate texture coordinates.
 */
static void triangle(void* ctx, const MilkVertex* v, int textured)
{
    (void)ctx;

    for (unsigned i = 0; i < 3; ++i)
    {
        vertex(v[i]);

        if (textured)
        {
            TEST_ASSERT_TRUE_MESSAGE(v[i].u >= 0 && v[i].u <= 1 && v[i].v >= 0 && v[i].v <= 1, "v[i].u >= 0 && v[i].u <= 1 && v[i].v >= 0 && v[i].v <= 1");
        }
    }

    area += fabs((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x)) * .5;

    ++triangles;
}

/**
 * @brief Validate and capture the latest custom line endpoints.
 *
 * @param ctx Unused callback context.
 * @param a Start vertex.
 * @param b End vertex.
 */
static void line(void* ctx, MilkVertex a, MilkVertex b)
{
    (void)ctx;
    vertex(a);
    vertex(b);

    first = a;
    last  = b;

    ++lines;
}

/**
 * @brief Run regression checks for custom object equations and clipping.
 *
 */
static void object_regressions(void)
{
    Preset        p;
    MusicFeatures audio = { 0 };

    preset_init(&p, (PresetKind)0, 12);

    p.milk.frame[ML_Q1] = 42;

    for (unsigned i = 0; i < 512; ++i)
    {
        audio.custom_wave[0][i] = .25f;
    }

    milk_objects_step(&p, &audio);

    const MilkObjectState* shape = &p.milk.objects[0];
    const MilkObjectState* wave  = &p.milk.objects[1];

    TEST_ASSERT_TRUE_MESSAGE(shape->count == 2 && shape->cache.shape[0][MO_X] == .25f && shape->cache.shape[1][MO_X] == .75f, "shape->count == 2 && shape->cache.shape[0][MO_X] == .25f && shape->cache.shape[1][MO_X] == .75f");
    TEST_ASSERT_TRUE_MESSAGE(shape->cache.shape[0][MO_R] == .25f && shape->cache.shape[1][MO_R] == .25f, "shape->cache.shape[0][MO_R] == .25f && shape->cache.shape[1][MO_R] == .25f");
    TEST_ASSERT_TRUE_MESSAGE(shape->cache.shape[1][MO_G] == 1 && shape->frame[MILK_OBJECT_BUILTINS] == 2, "shape->cache.shape[1][MO_G] == 1 && shape->frame[MILK_OBJECT_BUILTINS] == 2");
    TEST_ASSERT_TRUE_MESSAGE(wave->count == 4 && wave->cache.wave[0].x == 0 && wave->cache.wave[3].x == 640, "wave->count == 4 && wave->cache.wave[0].x == 0 && wave->cache.wave[3].x == 640");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(wave->cache.wave[0].y - (MILK_VIEWPORT_HALF_HEIGHT + (.25f * .512f + .1f) * 640)) < .001f, "fabsf(wave->cache.wave[0].y - (MILK_VIEWPORT_HALF_HEIGHT + (.25f * .512f + .1f) * 640)) < .001f");
    TEST_ASSERT_TRUE_MESSAGE(wave->cache.wave[0].r == 255 && wave->point[MO_T2] == 7 && wave->point[MILK_OBJECT_BUILTINS] == 4, "wave->cache.wave[0].r == 255 && wave->point[MO_T2] == 7 && wave->point[MILK_OBJECT_BUILTINS] == 4");
    TEST_ASSERT_TRUE_MESSAGE(p.milk.frame[ML_Q2] == 0 && wave->point[MO_TIME] == 0, "p.milk.frame[ML_Q2] == 0 && wave->point[MO_TIME] == 0");
    milk_objects_step(&p, &audio);
    TEST_ASSERT_TRUE_MESSAGE(shape->frame[MILK_OBJECT_BUILTINS] == 4 && wave->point[MILK_OBJECT_BUILTINS] == 8 && wave->point[MO_T2] == 7, "shape->frame[MILK_OBJECT_BUILTINS] == 4 && wave->point[MILK_OBJECT_BUILTINS] == 8 && wave->point[MO_T2] == 7");

    Preset       before = p;
    PresetCanvas canvas = { .opacity = 1, .object_triangle = triangle, .object_line = line };

    milk_objects_draw(&p, &canvas);
    TEST_ASSERT_TRUE_MESSAGE(triangles == 8 && lines == 6 && !memcmp(&before, &p, sizeof(p)), "triangles == 8 && lines == 6 && !memcmp(&before, &p, sizeof(p))");
    /* A wrapped textured triangle spans nine tiles, without gaps or overdraw. */
    MilkVertex tri[3] = { { 0, 0, 255, 0, 0, 1, -1, -1 }, { 640, 0, 0, 255, 0, 1, 2, -1 }, { 0, DISPLAY_HEIGHT, 0, 0, 255, 1, -1, 2 } };

    triangles = 0;
    area      = 0;

    milk_object_triangle(&canvas, tri, 1, 1);
    TEST_ASSERT_TRUE_MESSAGE(triangles > 3 && fabs(area - DISPLAY_WIDTH * DISPLAY_HEIGHT * .5) < 1, "triangles > 3 && fabs(area - DISPLAY_WIDTH * DISPLAY_HEIGHT * .5) < 1");

    triangles = 0;
    area      = 0;

    milk_object_triangle(&canvas, tri, 1, 0);
    TEST_ASSERT_TRUE_MESSAGE(fabs(area - DISPLAY_WIDTH * DISPLAY_HEIGHT * .5) < 1, "fabs(area - DISPLAY_WIDTH * DISPLAY_HEIGHT * .5) < 1");
    /* Clip colour/alpha along with geometry and apply transition opacity once. */
    MilkVertex a = { -100, 100, 0, 0, 0, 0, 0, 0 }, b = { 100, 100, 200, 100, 50, 1, 0, 0 };

    canvas.opacity = .5f;

    milk_object_line(&canvas, a, b);
    TEST_ASSERT_TRUE_MESSAGE(first.x == 0 && first.r == 100 && first.a == .25f && last.a == .5f, "first.x == 0 && first.r == 100 && first.a == .25f && last.a == .5f");
    puts("PASS: custom shape instances, separate frame/point/q/t scopes, stereo samples, pure drawing, wrapped texture coverage and interpolated clipping");
}

/**
 * @brief Fixtures are initialized by each scenario.
 */
void setUp(void)
{
    triangles = lines = 0;
    area              = 0;
    first = last = (MilkVertex){ 0 };
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
    RUN_TEST(object_regressions);

    return UNITY_END();
}
