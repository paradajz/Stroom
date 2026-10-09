#include "milkdrop/runtime.h"
#include "unity.h"
#include <string.h>

#ifndef TEST_PRESET_COUNT
#define TEST_PRESET_COUNT MILK_PRESET_COUNT
#endif

/**
 * @brief Derive requirements independently from the emitted custom-wave programs.
 */
static int needs_spectrum(PresetKind kind)
{
    const MilkProgram* program = &milk_programs[kind];

    for (unsigned i = 0; i < program->object_count; ++i)
    {
        const MilkObjectProgram* object = &program->objects[i];

        if (object->type == 1 && object->defaults[MO_SPECTRUM] != 0)
        {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Selected and transitioning presets match a reference that always computes all FFTs.
 */
static void conditional_analysis(void)
{
    static Audio           input;
    static MilkdropRuntime actual, reference;

    input.active        = 1;
    input.history_count = AUDIO_HISTORY;

    for (unsigned i = 0; i < AUDIO_HISTORY; ++i)
    {
        input.history[i][0] = (int16_t)((i * 173) % 12000 - 6000);
        input.history[i][1] = (int16_t)((i * 97) % 8000 - 4000);
    }

    for (unsigned kind = 0; kind < TEST_PRESET_COUNT; ++kind)
    {
        TEST_ASSERT_EQUAL_INT(needs_spectrum(kind), milk_programs[kind].custom_spectrum);

        for (int transitioning = 0; transitioning <= 1; ++transitioning)
        {
            memset(&actual, 0, sizeof(actual));

            actual.director.mode = DIRECTOR_FIXED;

            preset_init(&actual.director.current, kind, 123);
            preset_init(&actual.director.next, (kind + 1) % TEST_PRESET_COUNT, 456);

            actual.director.transitioning  = transitioning;
            actual.director.blend_duration = .04f;
            reference                      = actual;

            for (unsigned frame = 0; frame < 4; ++frame)
            {
                int   needed = needs_spectrum(actual.director.current.kind) ||
                               (actual.director.transitioning && needs_spectrum(actual.director.next.kind));
                float previous[2][MILK_AUDIO_BINS];

                memcpy(previous, actual.music.milk_audio.spectrum, sizeof(previous));
                milkdrop_runtime_step(&actual, &input, .02f);
                music_step(&reference.music, &input, .02f, 1);
                director_step(&reference.director, &reference.music, .02f);

                MusicFeatures expected = reference.music;

                if (!needed)
                {
                    memcpy(expected.milk_audio.spectrum, previous, sizeof(previous));
                }

                TEST_ASSERT_EQUAL_MEMORY(&expected, &actual.music, sizeof(expected));
                TEST_ASSERT_EQUAL_MEMORY(&reference.director, &actual.director, sizeof(actual.director));
            }
        }
    }
}

void setUp(void)
{}

void tearDown(void)
{}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(conditional_analysis);

    return UNITY_END();
}
