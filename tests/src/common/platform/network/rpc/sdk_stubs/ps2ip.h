#pragma once

#include <tcpip.h>
#include <sys/ioctl.h>

#define recv     rpc_peer_recv
#define recvfrom rpc_peer_recvfrom
#define socket   rpc_peer_socket
#define send     rpc_peer_send
#define sendto   rpc_peer_sendto

/**
 * @brief Record TCP bytes submitted by the IOP RPC server.
 * @param fd Expected fixture socket descriptor.
 * @param data Borrowed bytes copied into fixture-owned storage before return.
 * @param size Number of readable bytes within fixture capacity.
 * @param flags Unused socket flags.
 * @return The requested byte count.
 */
ssize_t rpc_peer_send(int fd, const void* data, size_t size, int flags);

/**
 * @brief Record UDP bytes submitted by the IOP RPC server.
 * @param fd Expected fixture socket descriptor.
 * @param data Borrowed bytes copied into fixture-owned storage before return.
 * @param size Number of readable bytes within fixture capacity.
 * @param flags Unused socket flags.
 * @param address Unused destination address.
 * @param length Unused destination address length.
 * @return The requested byte count.
 */
ssize_t rpc_peer_sendto(int fd, const void* data, size_t size, int flags, const struct sockaddr* address, socklen_t length);

/**
 * @brief Provide RPC peer recv for the EE/IOP fixture.
 */
ssize_t rpc_peer_recv(int fd, void* data, size_t size, int flags);

/**
 * @brief Provide RPC peer recvfrom for the EE/IOP fixture.
 */
ssize_t rpc_peer_recvfrom(int fd, void* data, size_t size, int flags, struct sockaddr* address, socklen_t* length);

/**
 * @brief Provide RPC peer socket for the EE/IOP fixture.
 */
int rpc_peer_socket(int domain, int type, int protocol);

/**
 * @brief Substitute disconnect in this host fixture.
 */
int disconnect(int fd);

/**
 * @brief Substitute ioctlsocket in this host fixture.
 */
int ioctlsocket(int fd, long command, void* argument);

/**
 * @brief Substitute ps2ip_getconfig in this host fixture.
 */
int ps2ip_getconfig(char* name, t_ip_info* info);

/**
 * @brief Substitute ps2ip_setconfig in this host fixture.
 */
int ps2ip_setconfig(t_ip_info* info);

/**
 * @brief Substitute dns_setserver in this host fixture.
 */
void dns_setserver(u8 index, const ip_addr_t* address);

/**
 * @brief Substitute dns_getserver in this host fixture.
 */
const ip_addr_t* dns_getserver(u8 index);
