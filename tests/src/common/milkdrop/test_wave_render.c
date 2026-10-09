#include "milkdrop/preset.h"
#include "milkdrop/director.h"
#include "unity.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned     lines, dots;
static PresetVertex first_end;

/**
 * @brief Count waveform segments and capture the first endpoint.
 *
 * @param context Unused callback context.
 * @param a Unused start vertex.
 * @param b End vertex.
 * @param alpha Opacity to validate.
 */
static void line(void* context, PresetVertex a, PresetVertex b, float alpha)
{
    (void)context;
    (void)a;
    TEST_ASSERT_TRUE_MESSAGE(alpha >= 0 && alpha <= 1, "alpha >= 0 && alpha <= 1");

    if (!lines)
    {
        first_end = b;
    }

    ++lines;
}

/**
 * @brief Count waveform dots and validate their size and opacity.
 *
 * @param context Unused callback context.
 * @param a Unused center vertex.
 * @param size Half-size in pixels.
 * @param alpha Opacity to validate.
 */
static void dot(void* context, PresetVertex a, float size, float alpha)
{
    (void)context;
    (void)a;
    TEST_ASSERT_TRUE_MESSAGE(size == .5f && alpha >= 0 && alpha <= 1, "size == .5f && alpha >= 0 && alpha <= 1");
    ++dots;
}

/**
 * @brief Verify waveform style, smoothing, thick/dot rendering, and morph breaks.
 */
static void rendering(void)
{
    static Preset        p;
    static MusicFeatures a;

    p.milk.frame[ML_WAVE_MODE] = 2;
    p.milk.frame[ML_WAVE_A]    = 4;
    p.milk.frame[ML_WAVE_X] = p.milk.frame[ML_WAVE_Y] = .5f;
    p.milk.frame[ML_WAVE_SCALE]                       = 1;
    p.milk.frame[ML_WAVE_R]                           = 2;
    p.milk.frame[ML_WAVE_G]                           = .5f;
    p.milk.frame[ML_WAVE_BRIGHTEN]                    = 1;

    MilkWaveStyle style;

    milk_wave_style(&p, &a, &style);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(style.alpha - .36f) < 1e-6f && style.r == 1 && style.g == .5f, "fabsf(style.alpha - .36f) < 1e-6f && style.r == 1 && style.g == .5f");

    p.milk.frame[ML_WAVE_MODE]      = 1;
    p.milk.frame[ML_WAVE_A]         = 2;
    p.milk.frame[ML_WAVE_MOD_ALPHA] = 1;
    p.milk.frame[ML_WAVE_MOD_END]   = 1;
    a.relative[0] = a.relative[1] = a.relative[2] = .5f;

    milk_wave_style(&p, &a, &style);
    TEST_ASSERT_TRUE_MESSAGE(style.alpha == 1, "style.alpha == 1");

    p.milk.frame[ML_WAVE_MODE]      = 2;
    p.milk.frame[ML_WAVE_MOD_ALPHA] = 0;

    for (unsigned i = 0; i < 576; ++i)
    {
        a.milk_audio.waveform[0][i] = 12 * sinf(i * .17f);
        a.milk_audio.waveform[1][i] = 9 * cosf(i * .21f);
    }

    PresetCanvas c = { 0 };

    c.line    = line;
    c.sprite  = dot;
    c.opacity = 1;

    Preset        before       = p;
    MusicFeatures audio_before = a;

    milk_wave_draw(&p, NULL, 0, &c, &a);
    TEST_ASSERT_TRUE_MESSAGE(lines == 2 * (480 - 1), "lines == 2 * (480 - 1)");

    float    pcm[2][576];
    MilkWave raw;

    milk_smooth_wave(&a, 1, 0, pcm);
    milk_wave_shape(&p, &a, pcm, &raw);

    float x = (raw.point[0].x + 1.15f * raw.point[1].x - .15f * raw.point[2].x) * .5f;

    TEST_ASSERT_TRUE_MESSAGE(fabsf(first_end.x - x) < .0001f, "fabsf(first_end.x - x) < .0001f");
    TEST_ASSERT_TRUE_MESSAGE(!memcmp(&p, &before, sizeof(p)) && !memcmp(&a, &audio_before, sizeof(a)), "!memcmp(&p, &before, sizeof(p)) && !memcmp(&a, &audio_before, sizeof(a))");

    p.milk.frame[ML_WAVE_THICK] = 1;
    lines                       = 0;

    milk_wave_draw(&p, NULL, 0, &c, &a);
    TEST_ASSERT_TRUE_MESSAGE(lines == 4 * 2 * (480 - 1), "lines == 4 * 2 * (480 - 1)");

    p.milk.frame[ML_WAVE_DOTS] = 1;

    milk_wave_draw(&p, NULL, 0, &c, &a);
    TEST_ASSERT_TRUE_MESSAGE(dots == 4 * (2 * 480 - 1), "dots == 4 * (2 * 480 - 1)");

    MilkWave old = { 0 }, next = { 0 }, out;

    old.count = next.count = 4;
    old.break_at           = 2;
    next.break_at          = 3;
    old.break_at2 = next.break_at2 = MILK_WAVE_MAX;

    for (unsigned i = 0; i < 4; ++i)
    {
        old.point[i]  = (MilkPoint){ i * 10, 0 };
        next.point[i] = (MilkPoint){ i * 20, 10 };
    }

    milk_wave_morph(&old, &next, 0, &out);
    TEST_ASSERT_TRUE_MESSAGE(!memcmp(&old, &out, sizeof(out)), "!memcmp(&old, &out, sizeof(out))");
    milk_wave_morph(&old, &next, 1, &out);
    TEST_ASSERT_TRUE_MESSAGE(!memcmp(&next, &out, sizeof(out)), "!memcmp(&next, &out, sizeof(out))");
    milk_wave_morph(&old, &next, .5f, &out);
    TEST_ASSERT_TRUE_MESSAGE(out.point[1].x == 13.75f && out.point[1].y == 5, "out.point[1].x == 13.75f && out.point[1].y == 5");
    TEST_ASSERT_TRUE_MESSAGE(out.break_at == 3 && out.break_at2 == 3, "out.break_at == 3 && out.break_at2 == 3");
}

/**
 * @brief Verify fixed-mode behavior, adaptive hard cuts, and eased blending.
 */
static void cuts(void)
{
    static Director      d;
    static MusicFeatures a;

    director_init(&d, 2);

    PresetKind first = d.current.kind;

    director_set_mode(&d, DIRECTOR_FIXED);

    d.hard_cuts   = 1;
    a.relative[0] = a.relative[1] = a.relative[2] = 6;

    director_step(&d, &a, .02f);
    TEST_ASSERT_EQUAL_INT(first, d.current.kind); /* HOLD blocks cuts. */
    TEST_ASSERT_FALSE(d.transitioning);

    d.mode = DIRECTOR_SHUFFLE;

    director_step(&d, &a, .02f);
    TEST_ASSERT_NOT_EQUAL(first, d.current.kind);
    TEST_ASSERT_EQUAL_INT(d.order[1], d.current.kind);
    TEST_ASSERT_FALSE(d.transitioning);
    TEST_ASSERT_EQUAL_FLOAT(0, d.elapsed);
    TEST_ASSERT_GREATER_THAN_FLOAT(9, d.hard_cut_threshold);

    PresetKind cut = d.current.kind;

    director_step(&d, &a, .02f);
    TEST_ASSERT_EQUAL_INT(cut, d.current.kind); /* Raised threshold blocks another cut. */
    TEST_ASSERT_FALSE(d.transitioning);

    float threshold = d.hard_cut_threshold;

    a.relative[0] = a.relative[1] = a.relative[2] = 1;

    director_step(&d, &a, .02f);
    TEST_ASSERT_TRUE_MESSAGE(d.hard_cut_threshold < threshold, "d.hard_cut_threshold < threshold");
    director_next(&d);

    d.blend_time = d.blend_duration * .25f;

    TEST_ASSERT_TRUE_MESSAGE(fabsf(director_mix(&d) - (.5f - .5f * cosf(3.14159265f * .25f))) < 1e-6f, "fabsf(director_mix(&d) - (.5f - .5f * cosf(3.14159265f * .25f))) < 1e-6f");
}

/**
 * @brief Run regression checks for waveform rendering and hard cuts.
 *
 */
/**
 * @brief Fixtures are initialized by each scenario.
 */
void setUp(void)
{}

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
    RUN_TEST(rendering);
    RUN_TEST(cuts);

    return UNITY_END();
}
