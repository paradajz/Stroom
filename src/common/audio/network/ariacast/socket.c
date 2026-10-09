#include "audio/network/ariacast/socket.h"
#include "audio/network/ariacast/server.h"
#include "platform/network/socket_options.h"
#include "platform/network/socket_error.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

int aria_socket_retry(int fd)
{
    int code = platform_socket_error(fd);

    return code == 0 || code == EAGAIN || code == EWOULDBLOCK || code == EINTR;
}

void aria_socket_error(AriaServer* server, int type, unsigned port, const char* stage, int code)
{
    snprintf(server->error, sizeof(server->error), "ARIA %s %u %s ERR %d", type == SOCK_STREAM ? "TCP" : "UDP", port, stage, code);
}

int aria_socket_bind(AriaServer* server, int type, unsigned port)
{
    int fd = socket(AF_INET, type, 0);

    if (fd < 0)
    {
        aria_socket_error(server, type, port, "SOCKET", errno);
        return ARIA_SOCKET_ERROR_CREATE;
    }

    int failure = ARIA_SOCKET_ERROR_NONBLOCKING;
    int yes     = 1;

    (void)setsockopt(fd, PLATFORM_SOCKET_LEVEL, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in address = { 0 };

    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (server->nonblocking(fd) != 0)
    {
        aria_socket_error(server, type, port, "NONBLOCK", errno);
    }
    else if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        failure = ARIA_SOCKET_ERROR_BIND;
        aria_socket_error(server, type, port, "BIND", errno);
    }
    else
    {
        return fd;
    }

    close(fd);

    return failure;
}
