#include "ui/shared/text_layout.h"
#include "unity.h"

/**
 * @brief No per-test setup is required.
 */
void setUp(void)
{}

/**
 * @brief No per-test cleanup is required.
 */
void tearDown(void)
{}

/**
 * @brief Verify text width.
 */
static void text_width(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0, ui_text_width("", 2));
    TEST_ASSERT_EQUAL_FLOAT(6, ui_text_width("A", 2));
    TEST_ASSERT_EQUAL_FLOAT(14, ui_text_width("AB", 2));
    TEST_ASSERT_EQUAL_FLOAT(67.5f, ui_text_width("SHUFFLE", 2.5f));
    TEST_ASSERT_EQUAL_FLOAT(38, ui_text_width("00:00", 2));
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(text_width);

    return UNITY_END();
}
