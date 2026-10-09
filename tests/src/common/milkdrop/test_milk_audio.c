#include "milkdrop/music.h"
#include "unity.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846

/**
 * @brief Fill the analysis history with a deterministic three-tone fixture.
 *
 * @param a Audio state to initialize.
 * @param scale Amplitude multiplier.
 */
static void signal(Audio* a, float scale)
{
    memset(a, 0, sizeof(*a));

    a->active        = 1;
    a->history_count = AUDIO_HISTORY;
    a->history_write = 137;

    for (unsigned i = 0; i < AUDIO_HISTORY; ++i)
    {
        double t = (double)i / AUDIO_RATE;

        a->history[(a->history_write + i) % AUDIO_HISTORY][0] = (int16_t)(scale *
                                                                          (4000 * sin(2 * PI * 500 * t) + 3000 * sin(2 * PI * 5000 * t) + 2000 * sin(2 * PI * 9000 * t)));
    }
}

/**
 * @brief Compute reference band sums with an independent double-precision DFT.
 *
 * Use a 576-point periodic window, 1024-point zero padding, and the reference equalizer.
 *
 * @param pcm 576 input waveform samples.
 * @param bands Destination low/mid/high band sums.
 */
static void reference_bands(const float* pcm, double bands[3])
{
    memset(bands, 0, 3 * sizeof(double));

    for (unsigned k = 0; k < 256; ++k)
    {
        double real = 0, imag = 0;

        for (unsigned i = 0; i < 576; ++i)
        {
            double x = pcm[i];

            x *= .5 - .5 * cos(2 * PI * i / 576);
            real += x * cos(2 * PI * k * i / 1024);
            imag -= x * sin(2 * PI * k * i / 1024);
        }

        unsigned band = k < 85 ? 0 : k < 170 ? 1
                                             : 2;

        bands[band] += -.02 * log((512 - k) / 512.0) * hypot(real, imag);
    }
}

/**
 * @brief Run regression checks for reference audio analysis.
 *
 */
static void milk_audio_regressions(void)
{
    static Audio a, quiet;

    signal(&a, 1);
    signal(&quiet, .1f);

    double expected[3];

    const float fps[] = { 30, 50, 60 };

    for (unsigned f = 0; f < 3; ++f)
    {
        MilkAudio s      = { 0 };
        double    avg[3] = { 0 }, slow[3] = { 0 };
        float     dt = 1 / fps[f];

        for (unsigned frame = 0; frame < 70; ++frame)
        {
            milk_audio_step(&s, &a, dt, 1);

            if (frame == 0)
            {
                reference_bands(s.waveform[0], expected);
            }
            else
            {
                for (unsigned b = 0; b < 3; ++b)
                {
                    expected[b] = s.immediate[b];
                }
            }

            for (unsigned b = 0; b < 3; ++b)
            {
                double r = pow(expected[b] > avg[b] ? .2 : .5, 30 * (double)dt);
                double l = pow(frame < 50 ? .9 : .992, 30 * (double)dt);

                avg[b]  = avg[b] * r + expected[b] * (1 - r);
                slow[b] = slow[b] * l + expected[b] * (1 - l);

                TEST_ASSERT_TRUE_MESSAGE(fabs(s.immediate[b] / expected[b] - 1) < .0001, "fabs(s.immediate[b] / expected[b] - 1) < .0001");
                TEST_ASSERT_TRUE_MESSAGE(fabs(s.relative[b] - expected[b] / slow[b]) < .0001, "fabs(s.relative[b] - expected[b] / slow[b]) < .0001");
                TEST_ASSERT_TRUE_MESSAGE(fabs(s.attenuated[b] - avg[b] / slow[b]) < .0001, "fabs(s.attenuated[b] - avg[b] / slow[b]) < .0001");
            }
        }
    }

    MilkAudio loud = { 0 }, low = { 0 };

    for (unsigned frame = 0; frame < 1500; ++frame)
    {
        milk_audio_step(&loud, &a, .02f, 1);
        milk_audio_step(&low, &quiet, .02f, 1);
    }

    for (unsigned i = 0; i < AUDIO_HISTORY; ++i)
    {
        a.history[i][0] *= 2;
        quiet.history[i][0] *= 2;
    }

    milk_audio_step(&loud, &a, .02f, 1);
    milk_audio_step(&low, &quiet, .02f, 1);

    for (unsigned b = 0; b < 3; ++b)
    {
        TEST_ASSERT_TRUE_MESSAGE(loud.relative[b] > 1.98f && loud.relative[b] < 2.01f, "loud.relative[b] > 1.98f && loud.relative[b] < 2.01f");
        TEST_ASSERT_TRUE_MESSAGE(fabsf(loud.relative[b] - low.relative[b]) < .002f, "fabsf(loud.relative[b] - low.relative[b]) < .002f");
        TEST_ASSERT_TRUE_MESSAGE(loud.attenuated[b] < loud.relative[b], "loud.attenuated[b] < loud.relative[b]");
    }

    /* Release follows the raw envelope before dividing by the new baseline. */
    float old_average[3], old_long[3];

    memcpy(old_average, loud.average, sizeof(old_average));
    memcpy(old_long, loud.long_average, sizeof(old_long));

    a.active = 0;

    milk_audio_step(&loud, &a, .02f, 1);

    for (unsigned b = 0; b < 3; ++b)
    {
        TEST_ASSERT_TRUE_MESSAGE(loud.relative[b] == 0, "loud.relative[b] == 0");

        float expected_release = old_average[b] * powf(.5f, .6f) / (old_long[b] * powf(.992f, .6f));

        TEST_ASSERT_TRUE_MESSAGE(fabsf(loud.attenuated[b] - expected_release) < .00001f, "fabsf(loud.attenuated[b] - expected_release) < .00001f");
    }

    /* Silence decays raw envelopes, and a zero denominator means neutral 1.
     * No right-channel energy or display-band/gain setting alters inputs. */
    MilkAudio silent = { 0 };

    a.active = 0;

    milk_audio_step(&silent, &a, .02f, 1);

    for (unsigned b = 0; b < 3; ++b)
    {
        TEST_ASSERT_TRUE_MESSAGE(silent.relative[b] == 1 && silent.attenuated[b] == 1, "silent.relative[b] == 1 && silent.attenuated[b] == 1");
    }

    signal(&a, 1);

    for (unsigned i = 0; i < AUDIO_HISTORY; ++i)
    {
        a.history[i][1] = a.history[i][0];
        a.history[i][0] = 0;
    }

    memset(&silent, 0, sizeof(silent));
    milk_audio_step(&silent, &a, .02f, 1);

    for (unsigned b = 0; b < 3; ++b)
    {
        TEST_ASSERT_TRUE_MESSAGE(silent.immediate[b] == 0, "silent.immediate[b] == 0");
    }

    puts("PASS: reference DFT, 30/50/60 Hz envelopes, quiet attacks, silence, left-channel isolation");
}

/**
 * @brief Skipping custom spectra preserves analysis history and resumes with fresh results.
 */
static void optional_spectra(void)
{
    static Audio input;
    MilkAudio    full = { 0 }, selective = { 0 };
    float        saved[2][MILK_AUDIO_BINS] = { 0 };

    for (unsigned frame = 0; frame < 80; ++frame)
    {
        signal(&input, .1f + frame * .01f);

        for (unsigned i = 0; i < AUDIO_HISTORY; ++i)
        {
            input.history[i][1] = input.history[(i + 17) % AUDIO_HISTORY][0] / 2;
        }

        int needed = frame == 0 || frame >= 60;

        milk_audio_step(&full, &input, .02f, 1);
        milk_audio_step(&selective, &input, .02f, needed);

        if (needed)
        {
            TEST_ASSERT_EQUAL_MEMORY(full.spectrum, selective.spectrum, sizeof(saved));
            memcpy(saved, selective.spectrum, sizeof(saved));
        }
        else
        {
            TEST_ASSERT_EQUAL_MEMORY(saved, selective.spectrum, sizeof(saved));
        }

        MilkAudio expected = full;

        memcpy(expected.spectrum, saved, sizeof(saved));
        TEST_ASSERT_EQUAL_MEMORY(&expected, &selective, sizeof(expected));
    }
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
    RUN_TEST(milk_audio_regressions);
    RUN_TEST(optional_spectra);

    return UNITY_END();
}
