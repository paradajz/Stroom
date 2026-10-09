#pragma once

#include <stddef.h>

void  rpc_peer_open(void);
void* rpc_peer_call(unsigned command, void* request, int size);
void  rpc_peer_receive(int result);
void  rpc_fixture_assert_locked(void);
void  rpc_fixture_lifecycle(int create_result, int delete_result, int rebooted);
int   rpc_fixture_creations(void);
int   rpc_fixture_deletions(void);
