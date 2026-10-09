#pragma once

#include "contracts/cd.h"
#include <stdint.h>
#include <stddef.h>

/** Failure codes for this API. */
typedef enum
{
    CD_FORMAT_ERROR_INVALID_BCD         = -1,
    CD_FORMAT_ERROR_INVALID_MSF         = -2,
    CD_FORMAT_ERROR_INVALID_HEADER      = -3,
    CD_FORMAT_ERROR_INVALID_TRACK_COUNT = -4,
    CD_FORMAT_ERROR_INVALID_TRACK       = -5,
    CD_FORMAT_ERROR_INVALID_TRACK_ORDER = -6,
    CD_FORMAT_ERROR_INVALID_LEAD_OUT    = -7,
} CdFormatError;

/* CD TOC records have ten bytes; the first three describe the disc. */
#define CD_TOC_ENTRY_BYTES    10
#define CD_TOC_HEADER_ENTRIES 3
#define CD_TOC_POINT_OFFSET   2
#define CD_TOC_MSF_OFFSET     7
#define CD_TOC_FIRST_TRACK    0xa0
#define CD_TOC_LAST_TRACK     0xa1
#define CD_TOC_LEAD_OUT       0xa2
#define CD_TOC_DATA_TRACK     0x40

/**
 * @brief Validated audio-track boundaries in LBA sectors (150-sector lead-in removed).
 */
typedef struct
{
    int count;                    /**< Number of tracks, 1..99; zero represents an empty TOC. */
    int start[CD_MAX_TRACKS + 1]; /**< Track starts indexed from zero, followed by the lead-out at start[count]. */
} CdToc;

/**
 * @brief Validate an audio-only CD table of contents and decode sector boundaries.
 *
 * @param out Destination, unchanged on failure.
 * @param raw Raw sceCdGetToc data.
 * @param size Available bytes in raw.
 * @return 0 on success, a negative CdFormatError on failure.
 */
int cd_parse_toc(CdToc* out, const uint8_t* raw, size_t size);
