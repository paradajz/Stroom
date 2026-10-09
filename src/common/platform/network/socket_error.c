#include "platform/network/socket_error.h"
#include "platform/network/socket_options.h"
#include <errno.h>
#include <sys/socket.h>

int platform_socket_error(int fd)
{
    if (errno != ENFILE)
    {
        return errno;
    }

    /* ps2ips collapses negative socket results; recover the IOP error. */
    int       code = 0;
    socklen_t size = sizeof(code);

    if (getsockopt(fd, PLATFORM_SOCKET_LEVEL, SO_ERROR, &code, &size) < 0)
    {
        return PS2_SOCKET_QUERY_ERROR_GET_OPTION;
    }

    if (size != sizeof(code))
    {
        errno = EIO;

        return PS2_SOCKET_QUERY_ERROR_INVALID_REPLY;
    }

    errno = code;

    return code;
}
