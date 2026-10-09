#include "audio/network/ariacast/diagnostics/diagnostics.h"
#include <string.h>

void aria_diagnostics_session(AriaDiagnostics* d)
{
    d->last_bytes_at = d->last_write_at = 0;
    d->sound_queued                     = -1;
    d->cover_revision                   = 0;

    memset(d->cover_phases, 0, sizeof(d->cover_phases));
}

void aria_diagnostics_output(AriaDiagnostics* d, uint32_t now, int pcm, int queued, int result)
{
    if (queued >= 0 || result < 0)
    {
        d->sound_queued = queued;
    }

    if (result < 0)
    {
        ++d->output_errors;
        return;
    }

    if (queued == 0 && d->last_write_at)
    {
        if (pcm)
        {
            ++d->empty_with_pcm;
        }
        else
        {
            ++d->empty_without_pcm;
        }
    }

    if (result > 0)
    {
        ++d->busy_writes;
        return;
    }

    if (d->last_write_at && (uint32_t)(now - d->last_write_at) > d->max_write_gap)
    {
        d->max_write_gap = now - d->last_write_at;
    }

    d->last_write_at = now;

    if (pcm)
    {
        ++d->pcm_writes;
    }
    else
    {
        ++d->silence_writes;
    }
}

void aria_diagnostics_cover(AriaDiagnostics* d, AriaDiagnosticCaptureRecord record)
{
    unsigned phase = record.data[DIAGNOSTIC_CAPTURE_COVER_PHASE], revision = record.data[DIAGNOSTIC_CAPTURE_COVER_METADATA_REVISION];

    if (!revision || phase < 1 || phase > DIAGNOSTIC_ARTWORK_PHASE_MAX)
    {
        return;
    }

    if (d->cover_revision != revision)
    {
        memset(d->cover_phases, 0, sizeof(d->cover_phases));

        d->cover_revision = revision;
    }

    ++d->cover_phases[phase - 1].count;

    if (record.data[DIAGNOSTIC_CAPTURE_COVER_DURATION_MS] > d->cover_phases[phase - 1].maximum)
    {
        d->cover_phases[phase - 1].maximum = record.data[DIAGNOSTIC_CAPTURE_COVER_DURATION_MS];
    }

    d->cover_phases[phase - 1].last = record;
}
