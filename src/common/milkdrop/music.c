#include "milkdrop/music.h"
#include <math.h>
#include <string.h>

void music_init(MusicFeatures* s)
{
    memset(s, 0, sizeof(*s));
}

void music_step(MusicFeatures* s, const Audio* a, float seconds, int custom_spectrum)
{
    if (!isfinite(seconds) || seconds <= 0)
    {
        return;
    }

    s->time += seconds;

    ++s->frame;
    milk_audio_step(&s->milk_audio, a, seconds, custom_spectrum);

    for (unsigned ch = 0; ch < 2; ++ch)
    {
        for (unsigned i = 0; i < MILK_AUDIO_SAMPLES; ++i)
        {
            s->custom_wave[ch][i] = s->milk_audio.waveform[ch][i] / 128.0f;
        }
    }

    for (unsigned i = 0; i < MILK_SPECTRUM_POINTS; ++i)
    {
        s->spectrum_left[i] = s->milk_audio.left_spectrum[2 * i] + s->milk_audio.left_spectrum[2 * i + 1];
    }

    memcpy(s->relative, s->milk_audio.relative, sizeof(s->relative));
    memcpy(s->attenuated, s->milk_audio.attenuated, sizeof(s->attenuated));
}
