#pragma once

#include "audio/network/ariacast/server.h"

/** Failure codes for this API. */
typedef enum
{
    ARIA_DISCOVERY_ERROR_RECEIVE = -1,
} AriaDiscoveryError;

/**
 * @brief Handle one discovery datagram and optional diagnostic request.
 * @param server Receiver.
 * @param address Advertised IPv4 address.
 * @return 0 on success, a negative AriaDiscoveryError on failure.
 */
int aria_discovery_step(AriaServer* server, const char* address);
