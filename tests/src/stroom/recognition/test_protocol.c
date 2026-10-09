#include "recognition/protocol.h"
#include "unity.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ID         "R5ZoxkYIj.9IlgyFd.SaQ9dcXnM-"
#define ART_PREFIX "{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"matched\",\"track_count\":1,\"album\":\"Album\",\"artist\":\"Artist\",\"tracks\":[{\"number\":1,\"title\":\"Song\",\"artist\":\"\"}],\"artwork\":"

static CdLookupDocument* document;

/**
 * @brief Prepare fixture state before each test.
 */
void setUp(void)
{
    document = calloc(1, sizeof(*document));

    TEST_ASSERT_NOT_NULL(document);
}

/**
 * @brief Finish fixture cleanup and resource checks after each test.
 */
void tearDown(void)
{
    free(document);
}

/**
 * @brief Parse a fixture text input through the component under test.
 */
static int parse(const char* text)
{
    return cd_lookup_document(document, text, strlen(text));
}

/**
 * @brief Verify required envelopes.
 */
static void required_envelopes(void)
{
    TEST_ASSERT_TRUE(parse("{\"protocol\":2,\"service\":\"stroom-cd-recognition\",\"ready\":true}"));
    TEST_ASSERT_EQUAL_INT(CD_DOCUMENT_SERVICE, document->kind);
    TEST_ASSERT_FALSE(parse("{\"protocol\":2,\"service\":\"other\",\"ready\":true}"));
    TEST_ASSERT_FALSE(parse("{\"protocol\":2,\"service\":\"stroom-cd-recognition\",\"ready\":false}"));
    TEST_ASSERT_TRUE(parse("{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"pending\",\"result_path\":\"/cd/v2/lookups/" ID "\",\"retry_after_seconds\":5}"));
    TEST_ASSERT_EQUAL_INT(CD_DOCUMENT_PENDING, document->kind);
    TEST_ASSERT_FALSE(parse("{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"pending\",\"result_path\":\"http://evil.test/\",\"retry_after_seconds\":5}"));
    TEST_ASSERT_FALSE(parse("{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"no_match\"}"));
    TEST_ASSERT_TRUE(parse("{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"no_match\",\"expires_in_seconds\":604800}"));
    TEST_ASSERT_EQUAL_UINT(604800u, document->expires_seconds);
    TEST_ASSERT_EQUAL_UINT(0, document->retry_seconds);
    TEST_ASSERT_FALSE(parse("{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"no_match\",\"expires_in_seconds\":604801}"));
    TEST_ASSERT_FALSE(parse("{\"protocol\":2,\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"no_match\",\"expires_in_seconds\":1}"));
}

/**
 * @brief Verify bounded retries are separate from cache expiry.
 */
static void bounded_retries_are_separate_from_cache_expiry(void)
{
    TEST_ASSERT_TRUE(parse("{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"pending\",\"result_path\":\"/cd/v2/lookups/" ID "\",\"retry_after_seconds\":604800}"));
    TEST_ASSERT_EQUAL_UINT(604800u, document->retry_seconds);
    TEST_ASSERT_FALSE(parse("{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"pending\",\"result_path\":\"/cd/v2/lookups/" ID "\",\"retry_after_seconds\":604801}"));
    TEST_ASSERT_EQUAL_UINT(0, document->retry_seconds);
    TEST_ASSERT_FALSE(parse("{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"no_match\",\"retry_after_seconds\":86400}"));
    TEST_ASSERT_FALSE(parse("{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"no_match\",\"expires_in_seconds\":0}"));
    TEST_ASSERT_TRUE(parse(ART_PREFIX "{\"status\":\"pending\",\"retry_after_seconds\":604800}}"));
    TEST_ASSERT_FALSE(parse(ART_PREFIX "{\"status\":\"pending\",\"retry_after_seconds\":604801}}"));
}

/**
 * @brief Verify artwork paths and unicode.
 */
static void artwork_paths_and_unicode(void)
{
    TEST_ASSERT_TRUE(parse(ART_PREFIX "{\"status\":\"available\",\"path\":\"/covers/release.jpg\"}}"));
    TEST_ASSERT_FALSE(parse(ART_PREFIX "{\"status\":\"available\",\"path\":\"//evil.test/cover.jpg\"}}"));
    TEST_ASSERT_FALSE(parse(ART_PREFIX "{\"status\":\"available\",\"path\":\"/covers/../cover.jpg\"}}"));
    TEST_ASSERT_FALSE(parse(ART_PREFIX "{\"status\":\"pending\"}}"));
    TEST_ASSERT_FALSE(parse(ART_PREFIX "{\"status\":\"unavailable\"},\"unknown\":\"\\ud800\"}"));
    TEST_ASSERT_FALSE(parse(ART_PREFIX "{\"status\":\"unavailable\"},\"unknown\":\"\\u0000\"}"));
    TEST_ASSERT_TRUE(parse(ART_PREFIX "{\"status\":\"unavailable\"},\"unknown\":[{\"a\":\"\\ud83c\\udfb5\"}]}"));
}

/**
 * @brief Verify full 99 track result.
 */
static void full_99_track_result(void)
{
    char* json = malloc(CD_LOOKUP_RESPONSE_BYTES + 1);

    TEST_ASSERT_NOT_NULL(json);

    unsigned used = (unsigned)sprintf(json, "{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"matched\",\"track_count\":99,\"album\":\"Album\",\"artist\":\"Artist\",\"tracks\":[");

    for (unsigned i = 0; i < 99; ++i)
    {
        used += (unsigned)sprintf(json + used, "%s{\"number\":%u,\"title\":\"", i ? "," : "", i + 1);

        for (unsigned n = 0; n < 127; ++n)
        {
            memcpy(json + used, "\\\"", 2);

            used += 2;
        }

        used += (unsigned)sprintf(json + used, "\",\"artist\":\"");

        for (unsigned n = 0; n < 127; ++n)
        {
            memcpy(json + used, "\\\\", 2);

            used += 2;
        }

        used += (unsigned)sprintf(json + used, "\"}");
    }

    used += (unsigned)sprintf(json + used, "],\"artwork\":{\"status\":\"unavailable\"}}");

    TEST_ASSERT_LESS_OR_EQUAL_UINT(CD_LOOKUP_RESPONSE_BYTES, used);
    TEST_ASSERT_TRUE(parse(json));
    TEST_ASSERT_EQUAL_UINT(99, document->count);
    TEST_ASSERT_EQUAL_UINT(127, strlen(document->tracks[98].title));

    json[used - 1] = '!';

    TEST_ASSERT_FALSE(parse(json));
    free(json);
}

/**
 * @brief Verify matched table is atomic and bounded.
 */
static void matched_table_is_atomic_and_bounded(void)
{
    const char* prefix = "{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"matched\",\"track_count\":1,\"album\":\"Album\",\"artist\":\"Artist\",\"tracks\":[{\"number\":1,\"title\":\"";
    char        json[2048];

    snprintf(json, sizeof(json), "%s\\ud83c\\udfb5\",\"artist\":\"\"}],\"artwork\":{\"status\":\"unavailable\"}}", prefix);
    TEST_ASSERT_TRUE(parse(json));
    TEST_ASSERT_EQUAL_UINT(4, strlen(document->tracks[0].title));
    snprintf(json, sizeof(json), "%s\\u0001\",\"artist\":\"\"}],\"artwork\":{\"status\":\"unavailable\"}}", prefix);
    TEST_ASSERT_FALSE(parse(json));

    char title[129];

    memset(title, 'A', 128);

    title[128] = 0;

    snprintf(json, sizeof(json), "%s%s\",\"artist\":\"\"}],\"artwork\":{\"status\":\"unavailable\"}}", prefix, title);
    TEST_ASSERT_FALSE(parse(json));
    TEST_ASSERT_FALSE(parse("{\"protocol\":2,\"disc_id\":\"" ID "\",\"status\":\"matched\",\"track_count\":1,\"album\":\"Album\",\"artist\":\"Artist\",\"tracks\":[{\"number\":2,\"title\":\"T\",\"artist\":\"\"}],\"artwork\":{\"status\":\"unavailable\"}}"));
}

/**
 * @brief Run this host-test suite and return its failure count.
 * @return Process exit status.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(required_envelopes);
    RUN_TEST(bounded_retries_are_separate_from_cache_expiry);
    RUN_TEST(artwork_paths_and_unicode);
    RUN_TEST(full_99_track_result);
    RUN_TEST(matched_table_is_atomic_and_bounded);

    return UNITY_END();
}
