#pragma once

#include <stdint.h>

/** Failure codes for this API. */
typedef enum
{
    IPV4_ERROR_OCTET_RANGE = -1,
    IPV4_ERROR_SYNTAX      = -2,
} Ipv4Error;

#define IPV4_DECIMAL_BASE 10

/**
 * @brief Parse a dotted IPv4 prefix without initializing SDK networking.
 * @param text Input cursor; advanced to the suffix only on success.
 * @param address Receives host-order address bits on success.
 * @return Zero on success, a negative Ipv4Error on malformed or out-of-range input.
 */
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
