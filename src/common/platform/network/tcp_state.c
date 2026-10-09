#include "platform/network/tcp_state.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

int platform_tcp_state(int fd, Ps2TcpState* state)
{
    memset(state, 0, sizeof(*state));

    socklen_t length = sizeof(*state);

    if (getsockopt(fd, IPPROTO_TCP, PS2_TCP_STATE_OPTION, state, &length) != 0 || length != sizeof(*state) || state->abi != PS2_TCP_STATE_ABI)
    {
        memset(state, 0, sizeof(*state));
        return 0;
    }

    return 1;
}
