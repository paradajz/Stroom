#include "audio/cd/cd_format.h"
#include "unity.h"
#include <string.h>

/**
 * @brief Encode a decimal fixture value as packed BCD.
 *
 * @param n Value in 0..99.
 * @return Packed BCD byte.
 */
static uint8_t bcd(unsigned n)
{
    return (n / 10) * 16 + n % 10;
}

/**
 * @brief Encode a sector address as CD minute/second/frame BCD bytes.
 *
 * @param p Destination of three bytes.
 * @param lba Sector address before adding the 150-sector lead-in.
 */
static void msf(uint8_t* p, unsigned lba)
{
    lba += 150;
    p[0] = bcd(lba / 4500);
    p[1] = bcd(lba / 75 % 60);
    p[2] = bcd(lba % 75);
}

/**
 * @brief Construct a valid audio-disc TOC fixture.
 *
 * @param raw Destination of at least 1024 bytes.
 * @param tracks Number of tracks to encode, 1..99.
 */
static void make_toc(uint8_t* raw, int tracks)
{
    memset(raw, 0, 1024);

    raw[2]  = 0xa0;
    raw[7]  = 1;
    raw[12] = 0xa1;
    raw[17] = bcd(tracks);
    raw[22] = 0xa2;

    msf(raw + 27, tracks * 3000);

    for (int i = 0; i < tracks; ++i)
    {
        raw[30 + i * 10] = 1;
        raw[32 + i * 10] = bcd(i + 1);

        msf(raw + 37 + i * 10, i * 3000);
    }
}

/**
 * @brief Verify TOC decoding and rejection of malformed or non-audio discs.
 */
static void toc_tests(void)
{
    uint8_t raw[1024];
    CdToc   toc = { 0 };

    make_toc(raw, 99);
    TEST_ASSERT_TRUE_MESSAGE(cd_parse_toc(&toc, raw, sizeof(raw)) == 0, "cd_parse_toc(&toc, raw, sizeof(raw))");
    TEST_ASSERT_TRUE_MESSAGE(toc.count == 99 && toc.start[0] == 0 && toc.start[98] == 294000 && toc.start[99] == 297000, "toc.count == 99 && toc.start[0] == 0 && toc.start[98] == 294000 && toc.start[99] == 297000");

    for (size_t n = 0; n < 1020; ++n)
    {
        TEST_ASSERT_TRUE_MESSAGE(cd_parse_toc(&toc, raw, n) != 0, "!cd_parse_toc(&toc, raw, n)");
    }

    make_toc(raw, 1);
    TEST_ASSERT_TRUE_MESSAGE(cd_parse_toc(&toc, raw, sizeof(raw)) == 0 && toc.start[1] == 3000, "cd_parse_toc(&toc, raw, sizeof(raw)) && toc.start[1] == 3000");

    raw[30] = 0x41;

    TEST_ASSERT_TRUE_MESSAGE(cd_parse_toc(&toc, raw, sizeof(raw)) != 0, "!cd_parse_toc(&toc, raw, sizeof(raw))");    // data track
    make_toc(raw, 2);

    raw[17] = 0xfa;

    TEST_ASSERT_TRUE_MESSAGE(cd_parse_toc(&toc, raw, sizeof(raw)) != 0, "!cd_parse_toc(&toc, raw, sizeof(raw))");
    make_toc(raw, 2);

    raw[38] = 0x60;

    TEST_ASSERT_TRUE_MESSAGE(cd_parse_toc(&toc, raw, sizeof(raw)) != 0, "!cd_parse_toc(&toc, raw, sizeof(raw))");
    make_toc(raw, 2);
    msf(raw + 47, 0);
    TEST_ASSERT_TRUE_MESSAGE(cd_parse_toc(&toc, raw, sizeof(raw)) != 0, "!cd_parse_toc(&toc, raw, sizeof(raw))");    // unordered tracks
    make_toc(raw, 2);
    msf(raw + 27, 2999);
    TEST_ASSERT_TRUE_MESSAGE(cd_parse_toc(&toc, raw, sizeof(raw)) != 0, "!cd_parse_toc(&toc, raw, sizeof(raw))");    // bad lead-out
    make_toc(raw, 2);

    raw[42] = 3;

    TEST_ASSERT_TRUE_MESSAGE(cd_parse_toc(&toc, raw, sizeof(raw)) != 0, "!cd_parse_toc(&toc, raw, sizeof(raw))");
    make_toc(raw, 2);

    raw[7] = 2;

    TEST_ASSERT_TRUE_MESSAGE(cd_parse_toc(&toc, raw, sizeof(raw)) != 0, "!cd_parse_toc(&toc, raw, sizeof(raw))");
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
    RUN_TEST(toc_tests);

    return UNITY_END();
}
