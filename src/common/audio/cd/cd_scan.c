#include "audio/cd/cd_scan.h"
#include "util/time_units.h"

#define SCAN_SPEED_MULTIPLIER 8

int cd_scan_step(CdScan* scan, int direction, uint32_t now, int position, const CdToc* toc)
{
    direction = (direction > 0) - (direction < 0);

    if (scan->direction && toc->count > 0)
    {
        uint64_t units = (uint64_t)(uint32_t)(now - scan->last_ms) * (CD_SECTORS_PER_SECOND * SCAN_SPEED_MULTIPLIER) + scan->remainder;
        int64_t  next  = (int64_t)position + scan->direction * (int64_t)(units / MILLISECONDS_PER_SECOND);

        scan->remainder = units % MILLISECONDS_PER_SECOND;

        if (next < toc->start[0])
        {
            next = toc->start[0];
        }

        if (next >= toc->start[toc->count])
        {
            next = toc->start[toc->count] - 1;
        }

        position = (int)next;
    }

    if (scan->direction != direction)
    {
        scan->remainder = 0;
    }

    scan->direction = direction;
    scan->last_ms   = now;

    return position;
}

int cd_track_at(const CdToc* toc, int position)
{
    if (!toc->count)
    {
        return 0;
    }

    int track = 1;

    while (track < toc->count && position >= toc->start[track])
    {
        ++track;
    }

    return track;
}
