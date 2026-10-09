#include "milkdrop/music.h"
#include "unity.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Verify shared-clock updates and MilkDrop equation input mapping.
 *
 */
static void music_regressions(void)
{
    static Audio  input;
    MusicFeatures response;
    MilkAudio     expected = { 0 };

    music_init(&response);

    input.active        = 1;
    input.history_count = AUDIO_HISTORY;

    for (unsigned i = 0; i < AUDIO_HISTORY; ++i)
    {
        input.history[i][0] = (int16_t)(12000 * sinf(i * 0.08f));
        input.history[i][1] = (int16_t)(6000 * cosf(i * 0.17f));
    }

    for (unsigned frame = 0; frame < 20; ++frame)
    {
        milk_audio_step(&expected, &input, 0.02f, 1);
        music_step(&response, &input, 0.02f, 1);
        TEST_ASSERT_TRUE_MESSAGE(response.frame == frame + 1, "response.frame == frame + 1");
        TEST_ASSERT_TRUE_MESSAGE(fabsf(response.time - (frame + 1) * 0.02f) < 0.000001f, "fabsf(response.time - (frame + 1) * 0.02f) < 0.000001f");
        TEST_ASSERT_TRUE_MESSAGE(memcmp(&response.milk_audio, &expected, sizeof(expected)) == 0, "memcmp(&response.milk_audio, &expected, sizeof(expected)) == 0");

        for (unsigned band = 0; band < 3; ++band)
        {
            TEST_ASSERT_TRUE_MESSAGE(response.relative[band] == expected.relative[band], "response.relative[band] == expected.relative[band]");
            TEST_ASSERT_TRUE_MESSAGE(response.attenuated[band] == expected.attenuated[band], "response.attenuated[band] == expected.attenuated[band]");
        }

        for (unsigned ch = 0; ch < 2; ++ch)
        {
            for (unsigned i = 0; i < MILK_AUDIO_SAMPLES; ++i)
            {
                TEST_ASSERT_TRUE_MESSAGE(response.custom_wave[ch][i] == expected.waveform[ch][i] / 128.0f, "response.custom_wave[ch][i] == expected.waveform[ch][i] / 128.0f");
            }
        }

        for (unsigned i = 0; i < MILK_SPECTRUM_POINTS; ++i)
        {
            TEST_ASSERT_TRUE_MESSAGE(response.spectrum_left[i] == expected.left_spectrum[2 * i] + expected.left_spectrum[2 * i + 1], "response.spectrum_left[i] == expected.left_spectrum[2 * i] + expected.left_spectrum[2 * i + 1]");
        }
    }

    MusicFeatures snapshot = response;

    music_step(&response, &input, 0, 1);
    music_step(&response, &input, -1, 1);
    music_step(&response, &input, NAN, 1);
    music_step(&response, &input, INFINITY, 1);
    TEST_ASSERT_TRUE_MESSAGE(memcmp(&response, &snapshot, sizeof(response)) == 0, "memcmp(&response, &snapshot, sizeof(response)) == 0");
    music_init(&response);

    MusicFeatures empty = { 0 };

    TEST_ASSERT_TRUE_MESSAGE(memcmp(&response, &empty, sizeof(response)) == 0, "memcmp(&response, &empty, sizeof(response)) == 0");
    puts("PASS: MilkDrop input mapping, shared clock, invalid timestep and reset");
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
    RUN_TEST(music_regressions);

    return UNITY_END();
}
