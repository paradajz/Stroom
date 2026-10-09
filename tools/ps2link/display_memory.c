#include <dmaKit.h>
#include <kernel.h>
#include <malloc.h>
#include <stdlib.h>

/* PS2Link can live below 1 MiB, where the EE has no accelerated uncached
 * mapping. Keep gsKit setup packets cached and use cache-flushed DMA instead.
 * These linker wrappers apply only to the launcher, not the player or SDK. */
// Linker --wrap symbols must preserve the SDK function names exactly.
// NOLINTBEGIN(readability-identifier-naming)
void* __wrap_gsKit_alloc_ucab(int size)
{
    return memalign(64, size);
}

void __wrap_gsKit_free_ucab(void* data)
{
    free(data);
}

void __wrap_dmaKit_send_ucab(u16 channel, void* data, u32 size)
{
    dmaKit_send(channel, data, size);
}

void __wrap_dmaKit_send_chain_ucab(u16 channel, void* data)
{
    /* The chain API has no extent. Flush all cached packet writes before DMA;
     * passing zero avoids inventing a contiguous length for the chain. */
    FlushCache(0);
    dmaKit_send_chain(channel, data, 0);
}

// NOLINTEND(readability-identifier-naming)
