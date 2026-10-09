#include "ui/motion.h"
#include "ui/presentation.h"
#include "unity.h"

void setUp(void)
{}

void tearDown(void)
{}

static void smooth_reversible_slide(void)
{
    UiMotion motion = { 0 };
    uint32_t at     = UINT32_MAX - 100;

    TEST_ASSERT_EQUAL_FLOAT(1, ui_motion_step(&motion, 1, at));
    TEST_ASSERT_EQUAL_FLOAT(1, ui_motion_step(&motion, 0, at));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, ui_motion_step(&motion, 0, at + 140));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, ui_motion_step(&motion, 1, at + 140));
    TEST_ASSERT_EQUAL_FLOAT(1, ui_motion_step(&motion, 1, at + 280));
    TEST_ASSERT_EQUAL_FLOAT(0, ui_motion_step(&motion, 0, at + 560));
    TEST_ASSERT_EQUAL_FLOAT(0, ui_motion_step(&motion, 0, at + 1000));
    TEST_ASSERT_EQUAL_FLOAT(1, ui_motion_step(&motion, 1, at + 2000));
}

int main(void)
{
    UNITY_BEGIN();
    TEST_ASSERT_EQUAL_UINT(12, ui_background_work_budget(0));
    TEST_ASSERT_EQUAL_UINT(8, ui_background_work_budget(4));
    TEST_ASSERT_EQUAL_UINT(4, ui_background_work_budget(16));
    TEST_ASSERT_EQUAL_UINT(4, ui_background_work_budget(UINT32_MAX));
    RUN_TEST(smooth_reversible_slide);

    return UNITY_END();
}
