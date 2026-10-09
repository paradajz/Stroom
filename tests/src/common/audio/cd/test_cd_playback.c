#include "audio/cd/cd_playback.h"
#include "unity.h"

static CdPlayback playback;

/**
 * @brief Start each case with a sequential 14-track disc and repeat disabled.
 */
void setUp(void)
{
    playback = (CdPlayback){ 0 };

    cd_playback_mode(&playback, CD_CONTINUE, 14, 1);
}

/**
 * @brief No external resources are owned by the playback policy.
 */
void tearDown(void)
{}

/**
 * @brief Automatic playback stops at lead-out; manual navigation stays in range.
 */
static void sequential_boundaries(void)
{
    TEST_ASSERT_EQUAL_INT(2, cd_playback_next(&playback, 1, 1, 1));
    TEST_ASSERT_EQUAL_INT(0, cd_playback_next(&playback, 14, 1, 1));
    TEST_ASSERT_EQUAL_INT(14, cd_playback_next(&playback, 14, 1, 0));
    TEST_ASSERT_EQUAL_INT(1, cd_playback_next(&playback, 1, -1, 0));
}

/**
 * @brief Repeat-one repeats automatically but permits manual track changes.
 */
static void repeat_one(void)
{
    playback.repeat = CD_REPEAT_ONE;

    TEST_ASSERT_EQUAL_INT(5, cd_playback_next(&playback, 5, 1, 1));
    TEST_ASSERT_EQUAL_INT(6, cd_playback_next(&playback, 5, 1, 0));
}

/**
 * @brief Repeat-all wraps forward and backward at disc boundaries.
 */
static void repeat_all(void)
{
    playback.repeat = CD_REPEAT_ALL;

    TEST_ASSERT_EQUAL_INT(1, cd_playback_next(&playback, 14, 1, 1));
    TEST_ASSERT_EQUAL_INT(14, cd_playback_next(&playback, 1, -1, 0));
}

/**
 * @brief Shuffle retains the current track first and visits every track once.
 */
static void shuffle_order(void)
{
    cd_playback_mode(&playback, CD_SHUFFLE, 99, 42);
    TEST_ASSERT_EQUAL_INT(42, playback.order[0]);

    int seen[100] = { 0 };

    for (int i = 0; i < 99; ++i)
    {
        int track = playback.order[i];

        TEST_ASSERT_GREATER_OR_EQUAL_INT(1, track);
        TEST_ASSERT_LESS_OR_EQUAL_INT(99, track);
        TEST_ASSERT_EQUAL_INT(0, seen[track]);
        ++seen[track];
        TEST_ASSERT_EQUAL_INT(i == 98 ? 0 : playback.order[i + 1], cd_playback_next(&playback, track, 1, 1));
    }
}

/**
 * @brief Program navigation follows the selected order and repeat policy.
 */
static void program_order(void)
{
    int list[] = { 7, 2, 9 };

    TEST_ASSERT_TRUE(cd_playback_program(&playback, list, 3, 14) == 0);
    TEST_ASSERT_EQUAL_INT(CD_PROGRAM, playback.mode);
    TEST_ASSERT_EQUAL_INT_ARRAY(list, playback.order, 3);
    TEST_ASSERT_EQUAL_INT(2, cd_playback_next(&playback, 7, 1, 1));
    TEST_ASSERT_EQUAL_INT(7, cd_playback_next(&playback, 2, -1, 0));
    TEST_ASSERT_EQUAL_INT(0, cd_playback_next(&playback, 9, 1, 1));

    playback.repeat = CD_REPEAT_ALL;

    TEST_ASSERT_EQUAL_INT(7, cd_playback_next(&playback, 9, 1, 1));
}

/**
 * @brief Verify rejection leaves the installed program and RNG unchanged.
 * @param list Proposed track order.
 * @param count Number of proposed entries.
 * @param total Disc track count.
 */
static void rejected_program(const int* list, unsigned count, int total)
{
    CdPlayback before = playback;

    TEST_ASSERT_TRUE(!(cd_playback_program(&playback, list, count, total) == 0));
    TEST_ASSERT_EQUAL_INT(before.mode, playback.mode);
    TEST_ASSERT_EQUAL_INT(before.repeat, playback.repeat);
    TEST_ASSERT_EQUAL_INT(before.count, playback.count);
    TEST_ASSERT_EQUAL_UINT32(before.random, playback.random);
    TEST_ASSERT_EQUAL_INT_ARRAY(before.order, playback.order, 99);
}

/**
 * @brief Reject duplicate, empty, oversized, and out-of-range programs.
 */
static void invalid_programs(void)
{
    int list[] = { 7, 2, 9 };
    int bad[]  = { 2, 2 };

    TEST_ASSERT_TRUE(cd_playback_program(&playback, list, 3, 14) == 0);
    rejected_program(bad, 2, 14);
    rejected_program(list, 0, 14);
    rejected_program(list, 100, 14);
    rejected_program(list, 3, 8);
}

static void saved_program_survives_modes(void)
{
    int list[] = { 3, 1, 2 };

    TEST_ASSERT_TRUE(cd_playback_program(&playback, list, 3, 5) == 0);
    cd_playback_mode(&playback, CD_SHUFFLE, 5, 2);
    cd_playback_mode(&playback, CD_CONTINUE, 5, 2);

    playback.repeat = CD_REPEAT_ALL;

    cd_playback_mode(&playback, CD_PROGRAM, 5, 2);
    TEST_ASSERT_EQUAL_INT(CD_PROGRAM, playback.mode);
    TEST_ASSERT_EQUAL_INT(CD_REPEAT_OFF, playback.repeat);
    TEST_ASSERT_EQUAL_INT(3, playback.count);
    TEST_ASSERT_EQUAL_INT_ARRAY(list, playback.order, 3);
}

/**
 * @brief Run playback ordering regressions.
 * @return Number of failed Unity cases.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(sequential_boundaries);
    RUN_TEST(repeat_one);
    RUN_TEST(repeat_all);
    RUN_TEST(shuffle_order);
    RUN_TEST(program_order);
    RUN_TEST(invalid_programs);
    RUN_TEST(saved_program_survives_modes);

    return UNITY_END();
}
