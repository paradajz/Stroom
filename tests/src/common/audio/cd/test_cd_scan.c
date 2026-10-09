#include "audio/cd/cd_scan.h"
#include "unity.h"
#include <stdint.h>

/**
 * @brief Run regression checks for scan timing and disc boundaries.
 *
 */
static void scan_regressions(void)
{
    CdToc  toc  = { .count = 3, .start = { 150, 900, 1650, 2400 } };
    CdScan scan = { 0 };
    int    p    = cd_scan_step(&scan, 1, 100, 750, &toc);

    TEST_ASSERT_TRUE_MESSAGE(p == 750, "p == 750");

    p = cd_scan_step(&scan, 1, 1100, p, &toc);

    TEST_ASSERT_TRUE_MESSAGE(p == 1350 && cd_track_at(&toc, p) == 2, "p == 1350 && cd_track_at(&toc, p) == 2");

    p = cd_scan_step(&scan, 0, 1600, p, &toc);

    TEST_ASSERT_TRUE_MESSAGE(p == 1650 && cd_track_at(&toc, p) == 3, "p == 1650 && cd_track_at(&toc, p) == 3");
    TEST_ASSERT_TRUE_MESSAGE(cd_scan_step(&scan, 0, 9999, p, &toc) == p, "cd_scan_step(&scan, 0, 9999, p, &toc) == p");
    // Direction reversal accounts for the old direction until the change.
    p = cd_scan_step(&scan, -1, 10000, p, &toc);
    p = cd_scan_step(&scan, 1, 11000, p, &toc);

    TEST_ASSERT_TRUE_MESSAGE(p == 1050, "p == 1050");

    p = cd_scan_step(&scan, 1, 12000, p, &toc);

    TEST_ASSERT_TRUE_MESSAGE(p == 1650, "p == 1650");
    TEST_ASSERT_TRUE_MESSAGE(cd_scan_step(&scan, 1, 20000, p, &toc) == 2399, "cd_scan_step(&scan, 1, 20000, p, &toc) == 2399");
    cd_scan_step(&scan, -1, 20000, 2399, &toc);
    TEST_ASSERT_TRUE_MESSAGE(cd_scan_step(&scan, -1, 30000, 2399, &toc) == 150, "cd_scan_step(&scan, -1, 30000, 2399, &toc) == 150");
    // Small steps retain fractional sectors, matching a single large step.
    scan = (CdScan){ 0 };
    p    = 150;

    cd_scan_step(&scan, 1, 0, p, &toc);

    for (unsigned i = 1; i <= 1000; ++i)
    {
        p = cd_scan_step(&scan, 1, i, p, &toc);
    }

    TEST_ASSERT_TRUE_MESSAGE(p == 750, "p == 750");

    scan = (CdScan){ 0 };

    cd_scan_step(&scan, 1, UINT32_MAX - 499, 150, &toc);
    TEST_ASSERT_TRUE_MESSAGE(cd_scan_step(&scan, 0, 500, 150, &toc) == 750, "cd_scan_step(&scan, 0, 500, 150, &toc) == 750");
    TEST_ASSERT_TRUE_MESSAGE(cd_track_at(&toc, 899) == 1 && cd_track_at(&toc, 900) == 2, "cd_track_at(&toc, 899) == 1 && cd_track_at(&toc, 900) == 2");

    CdToc empty = { 0 };

    scan = (CdScan){ 0 };

    TEST_ASSERT_TRUE_MESSAGE(cd_scan_step(&scan, 0, 0, 0, &empty) == 0 && cd_track_at(&empty, 0) == 0, "cd_scan_step(&scan, 0, 0, 0, &empty) == 0 && cd_track_at(&empty, 0) == 0");
}

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
    RUN_TEST(scan_regressions);

    return UNITY_END();
}
