#include "platform/memory/cache.h"
#include <kernel.h>

void platform_cache_writeback(void)
{
    FlushCache(WRITEBACK_DCACHE);
}
