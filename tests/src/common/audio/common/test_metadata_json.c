#include "audio/common/metadata_json.h"
#include "unity.h"
#include <string.h>
#include <stdio.h>

static TrackMetadata metadata;

/**
 * @brief Apply a terminated JSON fixture.
 * @param json Fixture document.
 * @return Parsed metadata action.
 */
static TrackMetadataAction apply(const char* json)
{
    return track_metadata_apply_json(&metadata, json, strlen(json));
}

/**
 * @brief Clear labels before each case.
 */
void setUp(void)
{
    memset(&metadata, 0, sizeof(metadata));
}

/**
 * @brief No resources require cleanup.
 */
void tearDown(void)
{}

/**
 * @brief AirMusic envelopes, partial updates and explicit clearing preserve field semantics.
 */
static void partial_updates(void)
{
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"data\":{\"title\":\"Live Audio\",\"artist\":\"AirMusic\",\"album\":\"Phone\",\"isPlaying\":true}}"));
    TEST_ASSERT_EQUAL_STRING("Live Audio", metadata.title);
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"data\":{\"artworkUrl\":\"http://phone/artwork.jpg\",\"isPlaying\":true}}"));
    TEST_ASSERT_EQUAL_STRING("Live Audio", metadata.title);
    TEST_ASSERT_EQUAL_STRING("AirMusic", metadata.artist);
    TEST_ASSERT_EQUAL_STRING("Phone", metadata.album);
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"artist\":null,\"title\":\"New track\",\"ignored\":[1,-2.4e+3,false,{},null]}"));
    TEST_ASSERT_EQUAL_STRING("", metadata.artist);
    TEST_ASSERT_EQUAL_STRING("Phone", metadata.album);
    TEST_ASSERT_EQUAL(TRACK_METADATA_GET, apply("{\"type\":\"get\"}"));
    TEST_ASSERT_EQUAL_STRING("New track", metadata.title);
    TEST_ASSERT_EQUAL(TRACK_METADATA_CLEAR, apply("{\"type\":\"clear\"}"));
    TEST_ASSERT_EQUAL_STRING("", metadata.title);
    TEST_ASSERT_EQUAL_STRING("", metadata.album);
}

/**
 * @brief Reject malformed documents without partially replacing labels.
 */
static void malformed(void)
{
    strcpy(metadata.title, "Original");

    const char* bad[] = { "", "[]", "{", "{\"title\":1}", "{\"title\":\"new\",}", "{\"title\":\"new\"} trailing", "{\"data\":null}", "{\"title\":\"a\",\"title\":\"b\"}", "{\"x\":01}", "{\"x\":1.}", "{\"x\":1e}", "{\"title\":\"\\uD800\"}", "{\"title\":\"\\uDC00\"}", "{\"title\":\"\\q\"}", "{\"title\":\"new\",\"album\":false}" };

    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        TEST_ASSERT_EQUAL_MESSAGE(TRACK_METADATA_INVALID, apply(bad[i]), bad[i]);
        TEST_ASSERT_EQUAL_STRING("Original", metadata.title);
    }

    char     deep[128];
    unsigned n = 0;

    n += (unsigned)sprintf(deep, "{\"x\":");

    for (unsigned i = 0; i < 20; ++i)
    {
        deep[n++] = '[';
    }

    deep[n++] = '0';

    for (unsigned i = 0; i < 20; ++i)
    {
        deep[n++] = ']';
    }

    deep[n++] = '}';
    deep[n]   = 0;

    TEST_ASSERT_EQUAL(TRACK_METADATA_INVALID, apply(deep));
}

/**
 * @brief Decode escaped Unicode, sanitize controls, truncate safely and serialize escaped text.
 */
static void strings(void)
{
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"title\":\"Beyonc\\u00e9 \\uD83C\\uDFB5\",\"artist\":\"A\\nB\\u0000C\",\"album\":\"\\\"quoted\\\"\\\\path\"}"));
    TEST_ASSERT_EQUAL_STRING("Beyoncé 🎵", metadata.title);
    TEST_ASSERT_EQUAL_STRING("A B C", metadata.artist);

    char json[TRACK_METADATA_JSON_BYTES];

    track_metadata_to_json(&metadata, json);

    TrackMetadata copy = metadata;

    memset(&metadata, 0, sizeof(metadata));
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply(json));
    TEST_ASSERT_EQUAL_MEMORY(&copy, &metadata, sizeof(copy));

    char long_json[512];

    memcpy(long_json, "{\"title\":\"", 10);
    memset(long_json + 10, 'a', 126);
    strcpy(long_json + 136, "éextra\"}");
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply(long_json));
    TEST_ASSERT_EQUAL_UINT(126, strlen(metadata.title));
    TEST_ASSERT_EQUAL_CHAR('a', metadata.title[125]);

    const unsigned char invalid[] = { '{', '"', 't', 'i', 't', 'l', 'e', '"', ':', '"', 0xc0, 0x80, '"', '}' };

    TEST_ASSERT_EQUAL(TRACK_METADATA_INVALID, track_metadata_apply_json(&metadata, invalid, sizeof(invalid)));
}

/**
 * @brief Artwork aliases merge independently of labels and reject truncated URLs.
 */
static void artwork_urls(void)
{
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"title\":\"Track\",\"artworkUrl\":\"http://192.168.1.33:8090/artwork.jpg?v=1\"}"));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.33:8090/artwork.jpg?v=1", metadata.artwork_url);
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"title\":\"Next\"}"));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.33:8090/artwork.jpg?v=1", metadata.artwork_url);
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"artwork_url\":null}"));
    TEST_ASSERT_EQUAL_STRING("", metadata.artwork_url);
    TEST_ASSERT_EQUAL_STRING("Next", metadata.title);

    char long_url[512];

    strcpy(long_url, "{\"artworkUrl\":\"");

    size_t prefix = strlen(long_url);

    memset(long_url + prefix, 'x', METADATA_ARTWORK_URL_BYTES);
    strcpy(long_url + prefix + METADATA_ARTWORK_URL_BYTES, "\"}");
    TEST_ASSERT_EQUAL(TRACK_METADATA_INVALID, apply(long_url));
    TEST_ASSERT_EQUAL_STRING("Next", metadata.title);
    TEST_ASSERT_EQUAL_STRING("", metadata.artwork_url);
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"artwork_url\":\"http://1.2.3.4/cover\"}"));

    char json[TRACK_METADATA_JSON_BYTES];

    track_metadata_to_json(&metadata, json);
    TEST_ASSERT_NOT_NULL(strstr(json, "http://1.2.3.4/cover"));
}

/**
 * @brief Ignore timing fields while preserving labels and omitting timing in replies.
 */
static void ignored_timing(void)
{
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"title\":\"Track\",\"positionMs\":157154,\"durationMs\":183000}"));

    TrackMetadata copy = metadata;

    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"data\":{\"positionMs\":158154,\"durationMs\":null}}"));
    TEST_ASSERT_EQUAL_MEMORY(&copy, &metadata, sizeof(copy));

    char json[TRACK_METADATA_JSON_BYTES];

    track_metadata_to_json(&metadata, json);
    TEST_ASSERT_NULL(strstr(json, "positionMs"));
    TEST_ASSERT_NULL(strstr(json, "durationMs"));
    TEST_ASSERT_EQUAL_STRING("Track", metadata.title);
}

/** Reject duplicate fields across envelopes and aliases without partial updates. */
static void mixed_fields(void)
{
    const char* bad[] = {
        "{\"title\":\"outer\",\"data\":{\"title\":\"inner\"}}",
        "{\"data\":{\"title\":\"inner\"},\"title\":\"outer\"}",
        "{\"artworkUrl\":\"a\",\"data\":{\"artwork_url\":\"b\"}}",
        "{\"data\":{\"artworkUrl\":null},\"artwork_url\":\"b\"}"
    };

    strcpy(metadata.title, "Original");

    TrackMetadata original = metadata;

    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i)
    {
        TEST_ASSERT_EQUAL_MESSAGE(TRACK_METADATA_INVALID, apply(bad[i]), bad[i]);
        TEST_ASSERT_EQUAL_MEMORY(&original, &metadata, sizeof(metadata));
    }

    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply("{\"title\":\"Track\",\"data\":{\"artist\":\"Artist\"}}"));
    TEST_ASSERT_EQUAL_STRING("Track", metadata.title);
    TEST_ASSERT_EQUAL_STRING("Artist", metadata.artist);
}

/** Shared metadata need not have passed through the sanitizing JSON parser. */
static void serialize_controls(void)
{
    char json[TRACK_METADATA_JSON_BYTES];

    strcpy(metadata.title, "line1\nline2\t\r\b\f\"\\");
    track_metadata_to_json(&metadata, json);
    TEST_ASSERT_NOT_NULL(strstr(json, "line1\\u000aline2\\u0009\\u000d\\u0008\\u000c\\\"\\\\"));

    for (unsigned c = 1; c < 32; ++c)
    {
        metadata.title[0] = (char)c;
        metadata.title[1] = 0;

        track_metadata_to_json(&metadata, json);

        char escaped[7];

        snprintf(escaped, sizeof(escaped), "\\u%04x", c);
        TEST_ASSERT_NOT_NULL(strstr(json, escaped));
        TEST_ASSERT_NULL(strchr(json, (int)c));
    }
}

/** Every field can reach its maximum escaped length without truncation or overwrite. */
static void serialize_capacity(void)
{
    struct
    {
        char          json[TRACK_METADATA_JSON_BYTES];
        unsigned char guard;
    } output;

    memset(&output, 0xa5, sizeof(output));

    char*        fields[] = { metadata.title, metadata.artist, metadata.album, metadata.artwork_url };
    const size_t sizes[]  = { sizeof(metadata.title), sizeof(metadata.artist), sizeof(metadata.album), sizeof(metadata.artwork_url) };
    char         empty[TRACK_METADATA_JSON_BYTES];

    track_metadata_to_json(&metadata, empty);

    size_t expected = strlen(empty);

    for (unsigned i = 0; i < sizeof(fields) / sizeof(*fields); ++i)
    {
        memset(fields[i], 1, sizes[i] - 1);
        fields[i][sizes[i] - 1] = 0;

        expected += 6 * (sizes[i] - 1);
    }

    track_metadata_to_json(&metadata, output.json);
    TEST_ASSERT_EQUAL_UINT(expected, strlen(output.json));
    TEST_ASSERT_EQUAL_HEX8(0xa5, output.guard);
    TEST_ASSERT_EQUAL_STRING("\"}}", output.json + expected - 3);
    TEST_ASSERT_EQUAL(TRACK_METADATA_UPDATE, apply(output.json));
    TEST_ASSERT_EQUAL_UINT(METADATA_TEXT_BYTES - 1, strlen(metadata.title));
    TEST_ASSERT_EQUAL_UINT(METADATA_ARTWORK_URL_BYTES - 1, strlen(metadata.artwork_url));
}

/**
 * @brief Run bounded metadata parser regressions.
 * @return Failed case count.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(mixed_fields);
    RUN_TEST(serialize_controls);
    RUN_TEST(serialize_capacity);
    RUN_TEST(artwork_urls);
    RUN_TEST(partial_updates);
    RUN_TEST(malformed);
    RUN_TEST(strings);
    RUN_TEST(ignored_timing);

    return UNITY_END();
}
