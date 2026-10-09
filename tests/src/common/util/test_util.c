#include "unity.h"
#include "util/parse.h"
#include "util/byte_order.h"
#include "util/json.h"
#include <limits.h>
#include <string.h>

/** @brief Fixtures are local to each utility scenario. */
void setUp(void)
{}

/** @brief No utility scenario retains resources. */
void tearDown(void)
{}

/** @brief Decimal spans preserve bounds, range checks, and outputs on failure. */
static void bounded_decimal_spans(void)
{
    const char digits[] = { '2', '5', '5', 'x' };
    unsigned   value    = 99;

    TEST_ASSERT_EQUAL_INT(0, util_decimal_parse(digits, digits + 3, 255, &value));
    TEST_ASSERT_EQUAL_UINT(255, value);

    value = 99;

    TEST_ASSERT_EQUAL_INT(UTIL_PARSE_INVALID, util_decimal_parse(digits, digits + 4, UINT_MAX, &value));
    TEST_ASSERT_EQUAL_UINT(99, value);

    const char*    valid[]    = { "0", "00012", "4294967295" };
    const unsigned expected[] = { 0, 12, UINT32_MAX };

    for (unsigned i = 0; i < 3; ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, util_decimal_parse(valid[i], valid[i] + strlen(valid[i]), UINT32_MAX, &value));
        TEST_ASSERT_EQUAL_UINT(expected[i], value);
    }

    const char* invalid[] = { "", "-1", "+1", " 1", "1 ", "1.0", "1e2" };

    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        value = 99;

        TEST_ASSERT_EQUAL_INT(UTIL_PARSE_INVALID, util_decimal_parse(invalid[i], invalid[i] + strlen(invalid[i]), UINT_MAX, &value));
        TEST_ASSERT_EQUAL_UINT(99, value);
    }

    const char*    overflow[] = { "256", "4294967296", "999999999999999999999999999999" };
    const unsigned limits[]   = { 255, UINT32_MAX, UINT32_MAX };

    for (unsigned i = 0; i < 3; ++i)
    {
        value = 99;

        TEST_ASSERT_EQUAL_INT(UTIL_PARSE_RANGE, util_decimal_parse(overflow[i], overflow[i] + strlen(overflow[i]), limits[i], &value));
        TEST_ASSERT_EQUAL_UINT(99, value);
    }

    const char zero[] = "0", one[] = "1";

    TEST_ASSERT_EQUAL_INT(0, util_decimal_parse(zero, zero + 1, 0, &value));
    TEST_ASSERT_EQUAL_UINT(0, value);
    TEST_ASSERT_EQUAL_INT(UTIL_PARSE_RANGE, util_decimal_parse(one, one + 1, 0, &value));
}

/** @brief Hex decoding is independent of locale and rejects non-digit bytes. */
static void hexadecimal_digits(void)
{
    TEST_ASSERT_EQUAL_INT(0, util_hex_digit('0'));
    TEST_ASSERT_EQUAL_INT(9, util_hex_digit('9'));
    TEST_ASSERT_EQUAL_INT(10, util_hex_digit('a'));
    TEST_ASSERT_EQUAL_INT(10, util_hex_digit('A'));
    TEST_ASSERT_EQUAL_INT(15, util_hex_digit('f'));
    TEST_ASSERT_EQUAL_INT(15, util_hex_digit('F'));
    TEST_ASSERT_EQUAL_INT(-1, util_hex_digit('g'));
    TEST_ASSERT_EQUAL_INT(-1, util_hex_digit('/'));
    TEST_ASSERT_EQUAL_INT(-1, util_hex_digit(0xff));
}

/** @brief Network words support unaligned storage without overwriting adjacent bytes. */
static void unaligned_network_words(void)
{
    uint8_t       bytes[]    = { 0x55, 0x01, 0x23, 0x45, 0x67, 0x66 };
    const uint8_t expected[] = { 0x55, 0x89, 0xab, 0xcd, 0xef, 0x66 };

    TEST_ASSERT_EQUAL_HEX32(0x01234567, util_read_be32(bytes + 1));
    util_write_be32(bytes + 1, 0x89abcdef);
    TEST_ASSERT_EQUAL_MEMORY(expected, bytes, sizeof(bytes));
}

/** @brief JSON callers retain their distinct control-character and NUL policies. */
static void json_text_policies(void)
{
    const char text[] = "\"A\\n\\u0000\\u007fZ\"";
    char       out[32];
    UtilJson   display = { (const unsigned char*)text, (const unsigned char*)text + strlen(text), UTIL_JSON_TEXT_DISPLAY };

    TEST_ASSERT_EQUAL_INT(1, util_json_string(&display, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("A   Z", out);
    TEST_ASSERT_EQUAL_PTR(display.end, display.at);

    UtilJson exact = { (const unsigned char*)text, (const unsigned char*)text + strlen(text), UTIL_JSON_TEXT_EXACT };

    TEST_ASSERT_EQUAL_INT(0, util_json_string(&exact, out, sizeof(out)));

    const char controls[] = "\"A\\nZ\"";
    exact                 = (UtilJson){ (const unsigned char*)controls, (const unsigned char*)controls + strlen(controls), UTIL_JSON_TEXT_EXACT };

    TEST_ASSERT_EQUAL_INT(1, util_json_string(&exact, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("A\nZ", out);
}

/** @brief Truncation keeps complete UTF-8 and continues validating the rest of the string. */
static void json_truncation_and_validation(void)
{
    const char text[] = "\"\\u20ac\\ud83d\\ude00\"";
    char       out[4];
    UtilJson   j = { (const unsigned char*)text, (const unsigned char*)text + strlen(text), UTIL_JSON_TEXT_EXACT };

    TEST_ASSERT_EQUAL_INT(2, util_json_string(&j, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("\xe2\x82\xac", out);
    TEST_ASSERT_EQUAL_PTR(j.end, j.at);

    const char invalid[] = "\"long\\ud800\"";
    j                    = (UtilJson){ (const unsigned char*)invalid, (const unsigned char*)invalid + strlen(invalid), UTIL_JSON_TEXT_EXACT };

    TEST_ASSERT_EQUAL_INT(0, util_json_string(&j, out, sizeof(out)));

    j      = (UtilJson){ (const unsigned char*)text, (const unsigned char*)text + strlen(text), UTIL_JSON_TEXT_EXACT };
    out[0] = 'x';

    TEST_ASSERT_EQUAL_INT(0, util_json_string(&j, out, 0));
    TEST_ASSERT_EQUAL_INT('x', out[0]);

    const char value[] = " {\"v\":[1,-2.3e+4,true,false,null,\"\\ud83d\\ude00\"]} ";
    j                  = (UtilJson){ (const unsigned char*)value, (const unsigned char*)value + strlen(value), UTIL_JSON_TEXT_EXACT };

    TEST_ASSERT_TRUE(util_json_skip(&j, 1));
    util_json_space(&j);
    TEST_ASSERT_EQUAL_PTR(j.end, j.at);
}

/** @brief Run portable utility API boundary checks. */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(bounded_decimal_spans);
    RUN_TEST(hexadecimal_digits);
    RUN_TEST(unaligned_network_words);
    RUN_TEST(json_text_policies);
    RUN_TEST(json_truncation_and_validation);

    return UNITY_END();
}
