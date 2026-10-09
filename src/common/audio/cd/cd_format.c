#include "audio/cd/cd_format.h"
#include "util/time_units.h"

#define BCD_RADIX      10
#define BCD_DIGIT_MASK 0x0f

/**
 * @brief Decode one packed BCD byte.
 *
 * @param x Packed decimal value.
 * @return Value in 0..99, or CD_FORMAT_ERROR_INVALID_BCD for an invalid digit.
 */
static int bcd(uint8_t x)
{
    return (x & BCD_DIGIT_MASK) >= BCD_RADIX || (x >> 4) >= BCD_RADIX ? CD_FORMAT_ERROR_INVALID_BCD : (x >> 4) * BCD_RADIX + (x & BCD_DIGIT_MASK);
}

/**
 * @brief Convert a BCD minute/second/frame address to a CD sector.
 *
 * @param p Three BCD bytes in MSF order.
 * @return Absolute sector with the 150-sector lead-in removed, or CD_FORMAT_ERROR_INVALID_MSF for invalid MSF.
 */
static int sector(const uint8_t* p)
{
    int m = bcd(p[0]), s = bcd(p[1]), f = bcd(p[2]);

    if (m < 0 || s < 0 || s >= SECONDS_PER_MINUTE || f < 0 || f >= CD_SECTORS_PER_SECOND)
    {
        return CD_FORMAT_ERROR_INVALID_MSF;
    }

    return (m * SECONDS_PER_MINUTE + s) * CD_SECTORS_PER_SECOND + f - CD_LEAD_IN_SECTORS;
}

int cd_parse_toc(CdToc* out, const uint8_t* raw, size_t size)
{
    CdToc toc = { 0 };

    if (size < CD_TOC_ENTRY_BYTES * CD_TOC_HEADER_ENTRIES || raw[CD_TOC_POINT_OFFSET] != CD_TOC_FIRST_TRACK || raw[CD_TOC_ENTRY_BYTES + CD_TOC_POINT_OFFSET] != CD_TOC_LAST_TRACK || raw[2 * CD_TOC_ENTRY_BYTES + CD_TOC_POINT_OFFSET] != CD_TOC_LEAD_OUT || bcd(raw[CD_TOC_MSF_OFFSET]) != 1)
    {
        return CD_FORMAT_ERROR_INVALID_HEADER;
    }

    toc.count = bcd(raw[CD_TOC_ENTRY_BYTES + CD_TOC_MSF_OFFSET]);

    if (toc.count < 1 || toc.count > CD_MAX_TRACKS || size < (unsigned)CD_TOC_ENTRY_BYTES * (CD_TOC_HEADER_ENTRIES + toc.count))
    {
        return CD_FORMAT_ERROR_INVALID_TRACK_COUNT;
    }

    for (int i = 0; i < toc.count; ++i)
    {
        const uint8_t* p = raw + CD_TOC_ENTRY_BYTES * (CD_TOC_HEADER_ENTRIES + i);

        /* Reject data tracks even on a drive reporting CDDA. */

        if ((p[0] & CD_TOC_DATA_TRACK) || bcd(p[CD_TOC_POINT_OFFSET]) != i + 1)
        {
            return CD_FORMAT_ERROR_INVALID_TRACK;
        }

        toc.start[i] = sector(p + CD_TOC_MSF_OFFSET);

        if (toc.start[i] < 0 || (i && toc.start[i] <= toc.start[i - 1]))
        {
            return CD_FORMAT_ERROR_INVALID_TRACK_ORDER;
        }
    }

    toc.start[toc.count] = sector(raw + 2 * CD_TOC_ENTRY_BYTES + CD_TOC_MSF_OFFSET);

    if (toc.start[toc.count] <= toc.start[toc.count - 1])
    {
        return CD_FORMAT_ERROR_INVALID_LEAD_OUT;
    }

    *out = toc;

    return 0;
}
