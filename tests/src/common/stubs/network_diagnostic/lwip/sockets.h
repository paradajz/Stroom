#pragma once

#include <arpa/inet.h>
#include <sys/socket.h>

#define lwip_ntohl ntohl
#define lwip_ntohs ntohs

int lwip_getpeername(int, struct sockaddr*, socklen_t*);
