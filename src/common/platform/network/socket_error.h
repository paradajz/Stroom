#pragma once

/** Failure codes for this API. */
typedef enum
{
    PS2_SOCKET_QUERY_ERROR_GET_OPTION    = -1,
    PS2_SOCKET_QUERY_ERROR_INVALID_REPLY = -2,
} Ps2SocketQueryError;

/**
 * @brief Resolve a failed socket operation's errno, querying SO_ERROR for SDK-collapsed ENFILE.
 *
 * @param fd Socket on which the operation failed; call before errno changes.
 * @return Resolved error, also stored in errno; zero means no pending socket error.
 * Minus one means the query failed (errno preserved) or its reply was malformed (EIO).
 * Callers own retry policy and must treat query failure separately from a resolved error.
 */
int platform_socket_error(int fd);
