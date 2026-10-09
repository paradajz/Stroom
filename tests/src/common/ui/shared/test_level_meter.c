#include "ui/shared/level_meter.h"
#include "unity.h"
#include <math.h>

static const float silence[2] = { 0, 0 };
static const float full[2]    = { 1, 1 };

void setUp(void)
{
    ui_level_update(silence, 0, 0);
}

void tearDown(void)
{}

/** Meter response uses elapsed time consistently across clock wrap. */
static void response_and_wrap(void)
{
    const uint32_t starts[] = { 0, UINT32_MAX - 30 };
    float          expected = 0;

    for (unsigned i = 0; i < 2; ++i)
    {
        ui_level_update(silence, 0, starts[i]);
        ui_level_update(silence, 1, starts[i]);
        ui_level_update(full, 1, starts[i] + 60);

        float attack;

        ui_level_values(0, &attack);
        TEST_ASSERT_TRUE(attack > .7f && attack < .9f);

        if (i)
        {
            TEST_ASSERT_EQUAL_FLOAT(expected, attack);
        }

        expected = attack;

        ui_level_update(silence, 1, starts[i] + 120);

        float release;

        ui_level_values(0, &release);
        TEST_ASSERT_TRUE(release < attack && release > attack / 2);
    }
}

/** Stereo channels use a bounded dBFS scale and inactive audio clears both. */
static void channel_values(void)
{
    const float stereo[2] = { 1, .001f };
    float       value;

    ui_level_values(0, &value);
    TEST_ASSERT_EQUAL_FLOAT(0, value);
    ui_level_update(stereo, 1, 0);
    ui_level_values(0, &value);
    TEST_ASSERT_EQUAL_FLOAT(1, value);
    ui_level_values(1, &value);
    TEST_ASSERT_EQUAL_FLOAT(0, value);
    ui_level_update(full, 1, 400);
    ui_level_values(1, &value);
    TEST_ASSERT_TRUE(value > .99f && value <= 1);
    ui_level_values(2, &value);
    TEST_ASSERT_EQUAL_FLOAT(0, value);
    ui_level_update(full, 0, 401);

    for (unsigned channel = 0; channel < 2; ++channel)
    {
        ui_level_values(channel, &value);
        TEST_ASSERT_EQUAL_FLOAT(0, value);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(response_and_wrap);
    RUN_TEST(channel_values);

    return UNITY_END();
}
