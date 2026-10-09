#pragma once

/**
 * @brief Write back the entire EE data cache to memory.
 *
 * Call where CPU writes must be visible to hardware. The caller owns DMA
 * ordering; this operation does not wait for transfers to complete.
 */
void platform_cache_writeback(void);
