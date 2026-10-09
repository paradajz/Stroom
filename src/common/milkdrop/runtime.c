#include "milkdrop/runtime.h"
#include "profiling/benchmark.h"
#include <string.h>

void milkdrop_runtime_reset_audio(MilkdropRuntime* state)
{
    float    time  = state->music.time;
    unsigned frame = state->music.frame;

    music_init(&state->music);

    state->music.time  = time;
    state->music.frame = frame;
}

void milkdrop_runtime_init(MilkdropRuntime* state, uint32_t seed)
{
    memset(state, 0, sizeof(*state));

    state->feedback = 1;

    director_init(&state->director, seed);
    milkdrop_runtime_reset_audio(state);
}

void milkdrop_runtime_step(MilkdropRuntime* state, const Audio* audio, float dt)
{
    if (dt > 0)
    {
        const Director* director        = &state->director;
        int             custom_spectrum = milk_programs[director->current.kind].custom_spectrum ||
                                          (director->transitioning && milk_programs[director->next.kind].custom_spectrum);

        PROFILE_BEGIN(audio_begin);
        music_step(&state->music, audio, dt, custom_spectrum);
        PROFILE_END(audio_analysis, audio_begin);
        director_step(&state->director, &state->music, dt);
    }
}
