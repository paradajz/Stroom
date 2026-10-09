#pragma once

#include <stdint.h>

/** Failure codes for this API. */
typedef enum
{
    IPV4_ERROR_OCTET_RANGE = -1,
    IPV4_ERROR_SYNTAX      = -2,
} Ipv4Error;

#define IPV4_DECIMAL_BASE 10

/* Parse a dotted IPv4 prefix without SDK network initialization. On success,
 * advance text to the suffix (e.g. URL port/path) and return host-order bits. */
static inline int util_ipv4_parse(const char** text, uint32_t* address)
{
    const char* at     = *text;
    uint32_t    parsed = 0;

    for (unsigned octet = 0; octet < 4; ++octet)
    {
        unsigned value = 0, digits = 0;

        while (*at >= '0' && *at <= '9')
        {
            value = value * IPV4_DECIMAL_BASE + (unsigned)(*at++ - '0');

            if (++digits > 3 || value > UINT8_MAX)
            {
                return IPV4_ERROR_OCTET_RANGE;
            }
        }

        parsed = (parsed << 8) | value;

        if (!digits || (octet < 3 && *at++ != '.'))
        {
            return IPV4_ERROR_SYNTAX;
        }
    }

    *text    = at;
    *address = parsed;

    return 0;
}
