#pragma once

#include "stroom_diagnostic_wire.h"
#include "lwip/tcp.h"
#include "lwip/prot/tcp.h"

/**
 * @brief Record the selected connection's incoming TCP header before processing.
 * @param pcb Matched connection.
 * @param seq Host-order sequence number.
 * @param ack Host-order acknowledgement.
 * @param length Payload bytes.
 * @param window Peer-advertised window.
 * @param flags TCP flags.
 */
void platform_net_diagnostic_rx(const struct tcp_pcb* pcb, uint32_t seq, uint32_t ack, unsigned length, unsigned window, unsigned flags);

/**
 * @brief Record an outgoing TCP header and IP submission result.
 * @param pcb Connection, possibly null for unrelated control packets.
 * @param header Header with network-order sequence and window fields.
 * @param header_bytes Contiguous bytes available from header, sampled before IP output.
 * @param result IP output result.
 */
void platform_net_diagnostic_tx(const struct tcp_pcb* pcb, const struct tcp_hdr* header, unsigned header_bytes, int result);

/**
 * @brief Record whether contiguous TCP bytes entered the socket mailbox.
 * @param pcb Connection.
 * @param length Contiguous payload bytes.
 * @param result Zero if enqueued, negative if refused.
 */
void platform_net_diagnostic_delivery(const struct tcp_pcb* pcb, unsigned length, int result);

/**
 * @brief Record a global input or pool failure while audio diagnostic recording is selected.
 * @param kind Input-error or pool-error event code.
 * @param value Error or pool identifier.
 */
void platform_net_diagnostic_failure(unsigned kind, int value);

/**
 * @brief Service the optional SDK diagnostic export in a brief interrupt-protected section.
 * @param request Versioned command.
 * @param reply Reply for select/poll, unused for read notifications.
 */
void platform_net_diagnostic_service(const Ps2DiagnosticRequest* request, Ps2DiagnosticReply* reply);
