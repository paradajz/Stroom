#pragma once

/** Failure codes for this API. */
typedef enum
{
    PS2_SOCKET_MODE_ERROR_NONBLOCKING = -1,
} Ps2SocketModeError;

/**
 * @brief Enable nonblocking operations on a socket through the PS2 SDK.
 * @param fd Socket descriptor.
 * @return 0 on success, a negative Ps2SocketModeError on failure.
 * Success leaves errno unchanged. Retry and cleanup policy belong to the caller.
 */
int platform_socket_nonblocking(int fd);
