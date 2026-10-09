#pragma once

#include "audio/cd/cd_format.h"
#include <stdint.h>

/**
 * @brief Time-based state for an 8x held CD scan.
 */
typedef struct
{
    int      direction; /**< -1 rewind, 0 idle, or 1 forward. */
    uint32_t last_ms;   /**< Last update timestamp in monotonic milliseconds. */
    unsigned remainder; /**< Fractional sector accumulator, in thousandths of a sector. */
} CdScan;

/**
 * @brief Advance a held scan at 8x and clamp to disc boundaries.
 *
 * @param scan Scan timing state to update.
 * @param direction Negative rewinds, zero releases, positive advances.
 * @param now Current monotonic time in milliseconds.
 * @param position Current absolute CD sector.
 * @param toc Disc track boundaries.
 * @return New sector position, including time spent in the previous direction.
 */
int cd_scan_step(CdScan* scan, int direction, uint32_t now, int position, const CdToc* toc);

/**
 * @brief Locate the track containing a sector, clamping to the first or last track.
 *
 * @param toc Disc track boundaries.
 * @param position Absolute CD sector.
 * @return One-based track number, or 0 for an empty TOC.
 */
int cd_track_at(const CdToc* toc, int position);
