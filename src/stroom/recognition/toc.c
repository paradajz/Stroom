#include "recognition/toc.h"
#include <stdio.h>
#include <limits.h>

#define LOOKUP_MAX_SECTORS (100 * 60 * CD_SECTORS_PER_SECOND)

unsigned cd_lookup_encode(const CdToc* toc, char* data, unsigned capacity)
{
    if (toc->count < 1 || toc->count > CD_MAX_TRACKS || !capacity)
    {
        return 0;
    }

    for (int i = 0; i <= toc->count; ++i)
    {
        if (toc->start[i] < 0 || toc->start[i] > INT_MAX - CD_LEAD_IN_SECTORS || (i && toc->start[i] <= toc->start[i - 1]))
        {
            return 0;
        }
    }

    if (toc->start[toc->count] > LOOKUP_MAX_SECTORS - CD_LEAD_IN_SECTORS)
    {
        return 0;
    }

    int n = snprintf(data, capacity, "{\"type\":\"" CD_LOOKUP_TYPE "\",\"first\":1,\"leadout\":%d,\"offsets\":[", toc->start[toc->count] + CD_LEAD_IN_SECTORS);

    if (n < 0 || (unsigned)n >= capacity)
    {
        return 0;
    }

    unsigned used = (unsigned)n;

    for (int i = 0; i < toc->count; ++i)
    {
        n = snprintf(data + used, capacity - used, "%s%d", i ? "," : "", toc->start[i] + CD_LEAD_IN_SECTORS);

        if (n < 0 || (unsigned)n >= capacity - used)
        {
            return 0;
        }

        used += (unsigned)n;
    }

    n = snprintf(data + used, capacity - used, "]}");

    return n < 0 || (unsigned)n >= capacity - used ? 0 : used + (unsigned)n;
}
