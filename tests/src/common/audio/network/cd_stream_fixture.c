/* Real transport fixture for the PC listener integration test. */
#include "audio/network/cd_stream/server.h"
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static CdStreamServer server;

int __real_bind(int fd, const struct sockaddr* address, socklen_t length);

/* Keep the real transport's TCP and UDP sockets private to this host test. */
int __wrap_bind(int fd, const struct sockaddr* address, socklen_t length)
{
    if (address->sa_family != AF_INET || length != sizeof(struct sockaddr_in))
    {
        return __real_bind(fd, address, length);
    }

    struct sockaddr_in local = *(const struct sockaddr_in*)address;
    local.sin_addr.s_addr    = htonl(INADDR_LOOPBACK);

    return __real_bind(fd, (const struct sockaddr*)&local, sizeof(local));
}

void platform_net_tx_stats(Ps2NetTxStats* stats)
{
    memset(stats, 0, sizeof(*stats));
}

static int nonblocking(int fd)
{
    int enabled = 1;

    return ioctl(fd, FIONBIO, &enabled);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    if (cd_stream_server_open(&server, nonblocking) != 0)
    {
        cd_stream_server_close(&server);
        return 1;
    }

    nonblocking(STDIN_FILENO);
    setbuf(stdout, NULL);
    puts("READY");

    unsigned previous  = 0;
    uint32_t now       = 0;
    int      available = 1;

    for (;;)
    {
        char command;
        int  n = read(STDIN_FILENO, &command, 1);

        if (n == 0 || (n > 0 && command == 'Q'))
        {
            break;
        }

        if (n > 0 && command == 'P')
        {
            uint8_t pcm[CD_STREAM_MAX_PAYLOAD_BYTES];

            for (unsigned i = 0; i < sizeof(pcm); ++i)
            {
                pcm[i] = (uint8_t)(i * 37);
            }

            cd_stream_server_push(&server, pcm);
        }
        else if (n > 0 && command == 'E')
        {
            cd_stream_server_end(&server);
        }
        else if (n > 0 && command == 'B')
        {
            puts("BARRIER");
        }
        else if (n > 0 && command == 'R')
        {
            cd_stream_server_reset(&server);
            puts("RESET");
        }
        else if (n > 0 && command == 'S')
        {
            cd_stream_server_close(&server);

            available = 0;

            puts("OFFLINE");
        }
        else if (n > 0 && command == 'O' && !available)
        {
            if (cd_stream_server_open(&server, nonblocking) != 0)
            {
                return 3;
            }

            available = 1;

            puts("ONLINE");
        }

        if (available && cd_stream_server_step(&server, now++, "127.0.0.1") != 0)
        {
            return 2;
        }

        unsigned clients = available ? cd_stream_server_clients(&server) : 0;

        if (clients != previous)
        {
            printf("CLIENTS %u\n", clients);

            previous = clients;
        }

        usleep(1000);
    }

    cd_stream_server_close(&server);

    return 0;
}
