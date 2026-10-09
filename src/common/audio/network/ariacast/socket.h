#pragma once

#include "audio/network/ariacast/server.h"

/** Failure codes for this API. */
typedef enum
{
    ARIA_SOCKET_ERROR_CREATE      = -1,
    ARIA_SOCKET_ERROR_NONBLOCKING = -2,
    ARIA_SOCKET_ERROR_BIND        = -3,
} AriaSocketError;

/**
 * @brief Resolve a negative nonblocking send/receive result.
 * @param fd Socket.
 * @return Nonzero when retrying is safe.
 */
int aria_socket_retry(int fd);

/**
 * @brief Record a socket failure before cleanup can overwrite errno.
 * @param server Receiver.
 * @param type Socket type.
 * @param port Service port.
 * @param stage Failed operation.
 * @param code Saved error code.
 */
void aria_socket_error(AriaServer* server, int type, unsigned port, const char* stage, int code);

/**
 * @brief Create a reusable nonblocking bound IPv4 socket.
 * @param server Receiver.
 * @param type Socket type.
 * @param port Port.
 * @return Descriptor on success, a negative AriaSocketError on failure.
 */
int aria_socket_bind(AriaServer* server, int type, unsigned port);
