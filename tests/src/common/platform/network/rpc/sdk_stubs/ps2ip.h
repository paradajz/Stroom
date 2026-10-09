#pragma once

#include <tcpip.h>
#include <sys/ioctl.h>

#define recv     rpc_peer_recv
#define recvfrom rpc_peer_recvfrom
#define socket   rpc_peer_socket

ssize_t          rpc_peer_recv(int fd, void* data, size_t size, int flags);
ssize_t          rpc_peer_recvfrom(int fd, void* data, size_t size, int flags, struct sockaddr* address, socklen_t* length);
int              rpc_peer_socket(int domain, int type, int protocol);
int              disconnect(int fd);
int              ioctlsocket(int fd, long command, void* argument);
int              ps2ip_getconfig(char* name, t_ip_info* info);
int              ps2ip_setconfig(t_ip_info* info);
void             dns_setserver(u8 index, const ip_addr_t* address);
const ip_addr_t* dns_getserver(u8 index);
