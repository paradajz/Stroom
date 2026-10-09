#pragma once

#include "audio/network/ariacast/server.h"
#include "contracts/diagnostic.h"
#include <netinet/in.h>

#if STROOM_DIAGNOSTICS
/**
 * @brief Reply to private inspection requests without affecting audio on send failure.
 * @param server Receiver.
 * @param query NUL-terminated query.
 * @param peer Validated sender address.
 * @return Nonzero when recognized.
 */
int aria_diagnostic_request(AriaServer* server, const char* query, const struct sockaddr_in* peer);
#else
/**
 * @brief Ignore private queries in release builds.
 * @param server Unused receiver.
 * @param query Unused query.
 * @param peer Unused sender.
 * @return Zero: private inspection is disabled.
 */
static inline int aria_diagnostic_request(AriaServer* server, const char* query, const struct sockaddr_in* peer)
{
    (void)server;
    (void)query;
    (void)peer;

    return 0;
}
#endif
