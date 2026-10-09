#include "milkdrop/preset.h"
#include "unity.h"
#include <float.h>
#include <math.h>

/**
 * @brief Initialize the lookup table through the public preset lifecycle.
 */
void setUp(void)
{
    static Preset preset;

    preset_init(&preset, (PresetKind)0, 1);
}

/**
 * @brief This suite owns no external resources.
 */
void tearDown(void)
{}

/**
 * @brief Preserve representative results recorded from the original lookup path.
 */
static void ordinary_angles(void)
{
    const float cases[][3] = {
        { -8192, 0x1.e99ba8p-1f, 0x1.2b810cp-2f },
        { -100, 0x1.03424cp-1f, 0x1.b9813p-1f },
        { -.125f, -0x1.feaa0ap-4f, 0x1.fc00c4p-1f },
        { 0, 0, 1 },
        { .125f, 0x1.feaa5cp-4f, 0x1.fc00c2p-1f },
        { 1, 0x1.aed53ap-1f, 0x1.14a274p-1f },
        { 100, -0x1.034248p-1f, 0x1.b980cap-1f },
        { 8192, -0x1.e99ba4p-1f, 0x1.2ac0d4p-2f },
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        /* Allow small host-libm differences in table initialization. */
        TEST_ASSERT_FLOAT_WITHIN(2e-7f, cases[i][1], preset_sin(cases[i][0]));
        TEST_ASSERT_FLOAT_WITHIN(2e-7f, cases[i][2], preset_cos(cases[i][0]));
    }
}

/**
 * @brief Check both sides of the lookup boundary against libm.
 */
static void lookup_boundary(void)
{
    const float angles[] = { nextafterf(8192, 0), 8192, nextafterf(8192, INFINITY) };

    for (unsigned i = 0; i < sizeof(angles) / sizeof(angles[0]); ++i)
    {
        for (int sign = -1; sign <= 1; sign += 2)
        {
            float angle = sign * angles[i];

            TEST_ASSERT_FLOAT_WITHIN(.002f, sinf(angle), preset_sin(angle));
            TEST_ASSERT_FLOAT_WITHIN(.002f, cosf(angle), preset_cos(angle));
        }
    }
}

/**
 * @brief Large finite angles use the correct unshifted libm function without unsafe casts.
 */
static void large_angles(void)
{
    const float angles[] = { nextafterf(8192, INFINITY), 1e7f, 1e8f, 1e30f, FLT_MAX };

    for (unsigned i = 0; i < sizeof(angles) / sizeof(angles[0]); ++i)
    {
        for (int sign = -1; sign <= 1; sign += 2)
        {
            float angle = sign * angles[i];

            TEST_ASSERT_EQUAL_FLOAT(sinf(angle), preset_sin(angle));
            TEST_ASSERT_EQUAL_FLOAT(cosf(angle), preset_cos(angle));
        }
    }
}

/**
 * @brief Invalid angles produce the sine/cosine pair for an identity rotation.
 */
static void nonfinite_angles(void)
{
    const float angles[] = { NAN, INFINITY, -INFINITY };

    for (unsigned i = 0; i < sizeof(angles) / sizeof(angles[0]); ++i)
    {
        TEST_ASSERT_EQUAL_FLOAT(0, preset_sin(angles[i]));
        TEST_ASSERT_EQUAL_FLOAT(1, preset_cos(angles[i]));
    }
}

/**
 * @brief Run feedback trigonometry regressions.
 * @return Number of failed Unity cases.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(ordinary_angles);
    RUN_TEST(lookup_boundary);
    RUN_TEST(large_angles);
    RUN_TEST(nonfinite_angles);

    return UNITY_END();
}
