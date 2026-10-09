#include "audio/network/ariacast/diagnostics/diagnostic_capture.h"
#include <stdio.h>
#include <string.h>

void aria_diagnostic_capture_tick(AriaDiagnosticCapture* f, uint32_t now)
{
    for (unsigned i = 0; i < DIAGNOSTIC_CAPTURE_EVENTS; ++i)
    {
        AriaDiagnosticCaptureEvent* e = &f->events[i];

        if (e->id && !e->complete && (uint32_t)(now - e->trigger) >= DIAGNOSTIC_CAPTURE_AFTER_MS)
        {
            e->complete = 1;
        }
    }
}

void aria_diagnostic_capture_record(AriaDiagnosticCapture* f, AriaDiagnosticCaptureRecord record)
{
    f->rolling[f->write] = record;
    f->write             = (f->write + 1) % DIAGNOSTIC_CAPTURE_RECORDS;

    if (f->count < DIAGNOSTIC_CAPTURE_RECORDS)
    {
        ++f->count;
    }

    for (unsigned i = 0; i < DIAGNOSTIC_CAPTURE_EVENTS; ++i)
    {
        AriaDiagnosticCaptureEvent* e = &f->events[i];

        if (!e->id || e->complete)
        {
            continue;
        }

        if (e->count < DIAGNOSTIC_CAPTURE_RECORDS)
        {
            e->records[e->count++] = record;
        }
        else
        {
            ++e->truncated;
        }
    }
}

void aria_diagnostic_capture_session(AriaDiagnosticCapture* f, uint32_t session)
{
    for (unsigned i = 0; i < DIAGNOSTIC_CAPTURE_EVENTS; ++i)
    {
        if (f->events[i].id)
        {
            f->events[i].complete = 1;
        }
    }

    f->write = f->count = 0;
    f->in_shortage      = 0;
    f->had_output       = 0;
    f->iop_records      = 0;
    f->sack_known = f->sack_enabled = f->sack_negotiated = 0;
    f->session                                           = session;
}

void aria_diagnostic_capture_output(AriaDiagnosticCapture* f, uint32_t now, uint32_t session, int pcm)
{
    int gap = f->had_output && (uint32_t)(now - f->last_output_at) >= DIAGNOSTIC_CAPTURE_OUTPUT_GAP_MS;

    f->last_output_at = now;
    f->had_output     = 1;

    if (pcm)
    {
        f->in_shortage = 0;

        if (!gap)
        {
            return;
        }
    }
    else
    {
        if (f->in_shortage)
        {
            return;
        }

        f->in_shortage = 1;
    }

    /* A continuing capture includes short recoveries and subsequent shortages. */

    for (unsigned i = 0; i < DIAGNOSTIC_CAPTURE_EVENTS; ++i)
    {
        if (f->events[i].id && !f->events[i].complete)
        {
            return;
        }
    }

    for (unsigned i = 0; i < DIAGNOSTIC_CAPTURE_EVENTS; ++i)
    {
        AriaDiagnosticCaptureEvent* e = &f->events[i];

        if (e->id)
        {
            continue;
        }

        if (++f->next_id == 0)
        {
            ++f->next_id;
        }

        e->reason          = pcm ? 2 : 1;
        e->history_limited = f->count == DIAGNOSTIC_CAPTURE_RECORDS && (uint32_t)(now - f->rolling[f->write].at) < DIAGNOSTIC_CAPTURE_BEFORE_MS;
        e->id              = f->next_id;
        e->session         = session;
        e->peer            = f->peer;
        e->port            = f->port;
        e->byte_base       = f->byte_base;
        e->trigger         = now;

        for (unsigned j = 0; j < f->count; ++j)
        {
            AriaDiagnosticCaptureRecord r = f->rolling[(f->write + DIAGNOSTIC_CAPTURE_RECORDS - f->count + j) % DIAGNOSTIC_CAPTURE_RECORDS];

            if ((uint32_t)(now - r.at) <= DIAGNOSTIC_CAPTURE_BEFORE_MS)
            {
                e->records[e->count++] = r;
            }
        }

        return;
    }

    ++f->missed;
}

int aria_diagnostic_capture_reply(AriaDiagnosticCapture* f, const char* query, char* reply, size_t capacity)
{
    if (!strcmp(query, DIAGNOSTIC_CAPTURE_CLEAR_QUERY))
    {
        /* Keep identifiers monotonic so an in-progress download detects clearing. */

        for (unsigned i = 0; i < DIAGNOSTIC_CAPTURE_EVENTS; ++i)
        {
            f->events[i].id = f->events[i].count = f->events[i].truncated = 0;
            f->events[i].complete                                         = 0;
        }

        f->missed = 0;

        snprintf(reply, capacity, "{\"diagnosticVersion\":%u,\"cleared\":true}", (unsigned)DIAGNOSTIC_CAPTURE_VERSION);
        return 1;
    }

    if (!strcmp(query, DIAGNOSTIC_CAPTURE_QUERY))
    {
        size_t      used      = (size_t)snprintf(reply, capacity, "{\"diagnosticVersion\":%u,\"missed\":%u,\"iopStatus\":%d,\"iopLost\":%u,\"iopRecords\":%u,\"sackKnown\":%u,\"sackEnabled\":%u,\"sackNegotiated\":%u,\"events\":[", (unsigned)DIAGNOSTIC_CAPTURE_VERSION, f->missed, f->iop_status, (unsigned)f->iop_lost, (unsigned)f->iop_records, f->sack_known, f->sack_enabled, f->sack_negotiated);
        const char* separator = "";

        for (unsigned i = 0; i < DIAGNOSTIC_CAPTURE_EVENTS && used < capacity; ++i)
        {
            AriaDiagnosticCaptureEvent* e = &f->events[i];

            if (!e->id)
            {
                continue;
            }

            used += (size_t)snprintf(reply + used, capacity - used, "%s{\"id\":%u,\"session\":%u,\"trigger\":%u,\"count\":%u,\"complete\":%d,\"truncated\":%u,\"peer\":%u,\"port\":%u,\"byteBase\":%u,\"reason\":%u,\"historyLimited\":%u}", separator, (unsigned)e->id, (unsigned)e->session, (unsigned)e->trigger, e->count, e->complete, e->truncated, (unsigned)e->peer, (unsigned)e->port, (unsigned)e->byte_base, e->reason, e->history_limited);
            separator = ",";
        }

        if (used < capacity)
        {
            snprintf(reply + used, capacity - used, "]}");
        }

        return 1;
    }

    unsigned id, offset;
    char     extra;

    if (sscanf(query, DIAGNOSTIC_CAPTURE_QUERY " %u %u %c", &id, &offset, &extra) != 2)
    {
        return 0;
    }

    for (unsigned i = 0; i < DIAGNOSTIC_CAPTURE_EVENTS; ++i)
    {
        AriaDiagnosticCaptureEvent* e = &f->events[i];

        if (e->id != id || !e->complete || offset > e->count)
        {
            continue;
        }

        size_t   used = (size_t)snprintf(reply, capacity, "{\"diagnosticVersion\":%u,\"id\":%u,\"offset\":%u,\"records\":[", (unsigned)DIAGNOSTIC_CAPTURE_VERSION, id, offset);
        unsigned end  = offset + DIAGNOSTIC_CAPTURE_PAGE_RECORDS < e->count ? offset + DIAGNOSTIC_CAPTURE_PAGE_RECORDS : e->count;

        for (unsigned j = offset; j < end && used < capacity; ++j)
        {
            AriaDiagnosticCaptureRecord* r = &e->records[j];

            used += (size_t)snprintf(reply + used, capacity - used, "%s[%u,%u", j == offset ? "" : ",", (unsigned)r->at, (unsigned)r->kind);

            for (unsigned field = 0; field < DIAGNOSTIC_CAPTURE_DATA_WORDS && used < capacity; ++field)
            {
                used += (size_t)snprintf(reply + used, capacity - used, ",%u", (unsigned)r->data[field]);
            }

            if (used < capacity)
            {
                used += (size_t)snprintf(reply + used, capacity - used, "]");
            }
        }

        if (used < capacity)
        {
            snprintf(reply + used, capacity - used, "]}");
        }

        return 1;
    }

    snprintf(reply, capacity, "{\"diagnosticVersion\":%u,\"id\":%u,\"offset\":%u,\"unavailable\":true}", (unsigned)DIAGNOSTIC_CAPTURE_VERSION, id, offset);

    return 1;
}
