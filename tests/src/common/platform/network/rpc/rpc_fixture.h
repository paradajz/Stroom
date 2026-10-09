#pragma once

#include <stddef.h>

/**
 * @brief Provide RPC peer open for the EE/IOP fixture.
 */
void rpc_peer_open(void);

/**
 * @brief Provide RPC peer call for the EE/IOP fixture.
 */
void* rpc_peer_call(unsigned command, void* request, int size);

/**
 * @brief Provide RPC peer receive for the EE/IOP fixture.
 */
void rpc_peer_receive(int result);

/**
 * @brief Inject an EE-to-IOP transfer result; a failure leaves old IOP bytes intact.
 * @param result Zero for a successful copy or a negative transfer failure.
 */
void rpc_peer_transfer(int result);

/**
 * @brief Read the number of socket sends made by the IOP peer.
 * @return Send-call count since rpc_peer_open().
 */
unsigned rpc_peer_sends(void);

/**
 * @brief Borrow the last bytes submitted to the IOP socket send substitute.
 * @return Fixture-owned storage, valid until the next send or peer reset.
 */
const unsigned char* rpc_peer_sent(void);

/**
 * @brief Provide RPC peer link for the EE/IOP fixture.
 */
void rpc_peer_link(int ready);

/**
 * @brief Provide RPC fixture assert locked for the EE/IOP fixture.
 */
void rpc_fixture_assert_locked(void);

/**
 * @brief Provide RPC fixture lifecycle for the EE/IOP fixture.
 */
void rpc_fixture_lifecycle(int create_result, int delete_result, int rebooted);

/**
 * @brief Provide RPC fixture creations for the EE/IOP fixture.
 */
int rpc_fixture_creations(void);

/**
 * @brief Provide RPC fixture deletions for the EE/IOP fixture.
 */
int rpc_fixture_deletions(void);
