#include "audio/output/device.h"

/* Source identities and playback policy stay in audio; the platform adapter
 * serializes hardware access using these identities as opaque producer tokens. */
int output_open(void)
{
    return platform_audio_output_open();
}

void output_select(OutputOwner owner)
{
    platform_audio_output_select(owner);
}

void output_set_muted(int muted)
{
    platform_audio_output_set_muted(muted);
}

int output_selected(OutputOwner owner)
{
    return platform_audio_output_selected(owner);
}

int output_initialize(const OutputRuntime* runtime)
{
    return platform_audio_output_initialize(runtime);
}

int output_prepare(OutputOwner owner, const OutputRuntime* runtime)
{
    return platform_audio_output_prepare(owner, runtime);
}

int output_stop(OutputOwner owner)
{
    return platform_audio_output_stop(owner);
}

int output_write(OutputOwner owner, const uint8_t* pcm, unsigned bytes)
{
    return platform_audio_output_write(owner, pcm, bytes);
}

#if STROOM_DIAGNOSTICS
int output_write_timed(OutputOwner owner, const uint8_t* pcm, unsigned bytes, OutputWriteTiming* timing)
{
    return platform_audio_output_write_timed(owner, pcm, bytes, timing);
}
#endif

int output_queued(OutputOwner owner)
{
    return platform_audio_output_queued(owner);
}

int output_close(void)
{
    return platform_audio_output_close();
}
