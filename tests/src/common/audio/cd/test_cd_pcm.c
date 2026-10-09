#include "audio/cd/cd_pcm.h"
#include "audio/common/pcm.h"
#include "audio/common/snapshot.h"
#include "unity.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Encode a signed PCM16 fixture sample in little-endian order.
 *
 * @param p Destination of two bytes.
 * @param value Signed sample value.
 */
static void put(uint8_t* p, int value)
{
    unsigned v = (uint16_t)(int16_t)value;

    p[0] = v;
    p[1] = v >> 8;
}

/**
 * @brief Decode a signed PCM16 fixture sample.
 *
 * @param p Pointer to two little-endian bytes.
 * @return Signed sample value.
 */
static int get(const uint8_t* p)
{
    unsigned v = p[0] | (unsigned)p[1] << 8;

    return v < 32768 ? (int)v : (int)v - 65536;
}

/**
 * @brief Verify CD resampling amplitude, timing, and sector continuity.
 */
static void resample_tests(void)
{
    uint8_t     in[CD_SECTOR_BYTES], out[CD_OUTPUT_BYTES];
    CdResampler r = { 0 };

    // A ramp crossing a sector boundary exposes lost phase/carry samples.

    for (int sector = 0; sector < 2; ++sector)
    {
        for (int i = 0; i < 588; ++i)
        {
            put(in + i * 4, sector * 588 + i);
            put(in + i * 4 + 2, -(sector * 588 + i));
        }

        cd_resample(&r, in, out);

        for (int i = 0; i < 640; ++i)
        {
            double expected = sector * 588 + i * 44100.0 / 48000 - 1;

            if (expected < 0)
            {
                expected = 0;
            }

            TEST_ASSERT_TRUE_MESSAGE(fabs(get(out + i * 4) - expected) < 1.01, "fabs(get(out + i * 4) - expected) < 1.01");
            TEST_ASSERT_TRUE_MESSAGE(fabs(get(out + i * 4 + 2) + expected) < 1.01, "fabs(get(out + i * 4 + 2) + expected) < 1.01");
        }
    }

    memset(&r, 0, sizeof(r));

    for (int i = 0; i < 588; ++i)
    {
        put(in + i * 4, -32768);
        put(in + i * 4 + 2, 32767);
    }

    cd_resample(&r, in, out);

    for (int i = 0; i < 640; ++i)
    {
        TEST_ASSERT_TRUE_MESSAGE(get(out + i * 4) == -32768, "get(out + i * 4) == -32768");
        TEST_ASSERT_TRUE_MESSAGE(get(out + i * 4 + 2) == 32767, "get(out + i * 4 + 2) == 32767");
    }

    // Resampled CD audio enters the canonical analysis pipeline directly.
    AudioBuffer cd = { 0 };

    audio_push_pcm(&cd, out, 640, 20);
    audio_analyze(&cd, 20);
    TEST_ASSERT_TRUE(cd.snapshot.active);
    TEST_ASSERT_EQUAL_UINT(640, cd.snapshot.history_count);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, cd.snapshot.rms[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, cd.snapshot.rms[1]);
    audio_analyze(&cd, 270);
    TEST_ASSERT_TRUE_MESSAGE(!cd.snapshot.active && cd.snapshot.history_count == 0, "!cd.snapshot.active && cd.snapshot.history_count == 0");
    // A 1 kHz CD tone must retain its period in the canonical 48 kHz PCM.
    memset(&cd, 0, sizeof(cd));
    memset(&r, 0, sizeof(r));

    for (int sector = 0; sector < 8; ++sector)
    {
        for (int i = 0; i < 588; ++i)
        {
            int value = (int)(16000 * sin(2 * 3.141592653589793 * 1000 * (sector * 588 + i) / 44100));

            put(in + i * 4, value);
            put(in + i * 4 + 2, value);
        }

        cd_resample(&r, in, out);
        audio_push_pcm(&cd, out, 640, sector * 13);
        audio_analyze(&cd, sector * 13);
    }

    unsigned first = 0, last = 0, crossings = 0;

    for (unsigned i = 1; i < cd.snapshot.history_count; ++i)
    {
        unsigned at       = (cd.snapshot.history_write + AUDIO_HISTORY - cd.snapshot.history_count + i) % AUDIO_HISTORY;
        unsigned previous = (at + AUDIO_HISTORY - 1) % AUDIO_HISTORY;

        if (cd.snapshot.history[previous][0] <= 0 && cd.snapshot.history[at][0] > 0)
        {
            if (!crossings)
            {
                first = i;
            }

            last = i;

            ++crossings;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(crossings > 2 && last > first, "crossings > 2 && last > first");
    TEST_ASSERT_TRUE_MESSAGE(fabs((double)AUDIO_RATE * (crossings - 1) / (last - first) - 1000) < 20, "fabs((double)AUDIO_RATE * (crossings - 1) / (last - first) - 1000) < 20");
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
    RUN_TEST(resample_tests);

    return UNITY_END();
}
