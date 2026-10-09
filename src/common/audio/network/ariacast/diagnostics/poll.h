#pragma once

#include "audio/network/ariacast/server.h"

#if STROOM_DIAGNOSTICS
/**
 * @brief Drain bounded IOP observations and sample receiver queues without changing playback.
 * @param server Worker-owned receiver.
 * @param now EE monotonic milliseconds.
 */
void aria_diagnostic_poll(AriaServer* server, uint32_t now);
#else
/**
 * @brief Leave release builds without recorder polling.
 * @param server Unused receiver.
 * @param now Unused time.
 */
static inline void aria_diagnostic_poll(AriaServer* server, uint32_t now)
{
    (void)server;
    (void)now;
}
#endif
