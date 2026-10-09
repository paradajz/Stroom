#include "audio/cd/lookup/toc.h"
#include "unity.h"
#include <string.h>

void setUp(void)
{}

void tearDown(void)
{}

static void exact_offsets(void)
{
    CdToc    toc = { .count = 2, .start = { 0, 15213, 32164 } };
    char     data[CD_LOOKUP_PACKET_BYTES];
    unsigned size = cd_lookup_encode(&toc, 7, data, sizeof(data));

    TEST_ASSERT_EQUAL_STRING("{\"type\":\"stroom-cd-toc\",\"version\":1,\"generation\":7,\"first\":1,\"leadout\":32314,\"offsets\":[150,15363]}", data);
    TEST_ASSERT_EQUAL_UINT(strlen(data), size);
    TEST_ASSERT_EQUAL_UINT(0, cd_lookup_encode(&toc, 7, data, size));

    toc.start[1] = toc.start[0];

    TEST_ASSERT_EQUAL_UINT(0, cd_lookup_encode(&toc, 7, data, sizeof(data)));
}

static void largest_disc(void)
{
    CdToc toc = { .count = CD_MAX_TRACKS };

    for (int i = 0; i <= toc.count; ++i)
    {
        toc.start[i] = i * 4500;
    }

    char data[CD_LOOKUP_PACKET_BYTES];

    TEST_ASSERT_GREATER_THAN_UINT(0, cd_lookup_encode(&toc, ~0u, data, sizeof(data)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(exact_offsets);
    RUN_TEST(largest_disc);

    return UNITY_END();
}
