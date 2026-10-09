#include "platform/network/socket_mode.h"
#include <ps2sdkapi.h>
#include <sys/ioctl.h>
#include <errno.h>

int platform_socket_nonblocking(int fd)
{
    unsigned long enabled = 1;
    int           result  = _ps2sdk_ioctl(fd, FIONBIO, &enabled);

    if (result < 0)
    {
        /* This SDK entry point returns negative errno without setting errno. */
        errno = -result;

        return PS2_SOCKET_MODE_ERROR_NONBLOCKING;
    }

    return 0;
}
