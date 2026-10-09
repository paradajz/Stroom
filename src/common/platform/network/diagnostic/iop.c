#include "stroom_diagnostic_hooks.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <intrman.h>
#include <string.h>

#define DIAGNOSTIC_SACK_INTERVAL_MS  1000
#define DIAGNOSTIC_TCP_OPTION_END    0
#define DIAGNOSTIC_TCP_OPTION_NOP    1
#define DIAGNOSTIC_TCP_OPTION_SACK   5
#define DIAGNOSTIC_TCP_OPTION_PREFIX 2
#define DIAGNOSTIC_TCP_SACK_BYTES    8
#define DIAGNOSTIC_TCP_SACK_BLOCKS   4
#define DIAGNOSTIC_TCP_MAX_HEADER    60

static int      sack_seen;
static unsigned sack_negotiated;
static uint32_t sack_at;
static uint32_t tx_serial;

static Ps2DiagnosticRecord history[DIAGNOSTIC_IOP_RING];
static unsigned            write_index;
static unsigned            record_count;
static uint32_t            lost_records;
static uint32_t            peer_address;
static unsigned            peer_port;
static int                 selected_socket = -1;
static uint32_t            delivered_bytes;
static uint32_t            read_bytes;

/**
 * @brief Check whether a TCP observation belongs to the selected audio peer.
 * @param pcb Connection or null.
 * @return Nonzero for the selected peer and local AriaCast port.
 */
static int selected(const struct tcp_pcb* pcb)
{
    return selected_socket >= 0 && pcb && pcb->local_port == DIAGNOSTIC_IOP_ARIA_PORT && pcb->remote_port == peer_port && lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&pcb->remote_ip))) == peer_address;
}

/**
 * @brief Append an observation without allocation or console output.
 * @param kind Event code.
 * @param data Fields indexed by the shared event contract.
 */
static void record(unsigned kind, const uint32_t data[DIAGNOSTIC_IOP_DATA_WORDS])
{
    int state;

    CpuSuspendIntr(&state);

    if (selected_socket >= 0)
    {
        Ps2DiagnosticRecord item = { .at = sys_now(), .kind = kind, .data = { 0 } };

        memcpy(item.data, data, sizeof(item.data));

        history[write_index] = item;
        write_index          = (write_index + 1) % DIAGNOSTIC_IOP_RING;

        if (record_count < DIAGNOSTIC_IOP_RING)
        {
            ++record_count;
        }
        else
        {
            ++lost_records;
        }
    }

    CpuResumeIntr(state);
}

/**
 * @brief Periodically retain the selected PCB's negotiated SACK state.
 * @param pcb Selected connection, protected by the caller.
 */
static void sack_state(const struct tcp_pcb* pcb)
{
    unsigned negotiated = 0;
#if LWIP_TCP_SACK_OUT
    negotiated = (pcb->flags & TF_SACK) != 0;
#endif
    uint32_t now = sys_now();

    if (!sack_seen || negotiated != sack_negotiated || (uint32_t)(now - sack_at) >= DIAGNOSTIC_SACK_INTERVAL_MS)
    {
        record(DIAGNOSTIC_IOP_SACK_STATE, (const uint32_t[DIAGNOSTIC_IOP_DATA_WORDS]){ [DIAGNOSTIC_IOP_SACK_STATE_ENABLED] = LWIP_TCP_SACK_OUT, [DIAGNOSTIC_IOP_SACK_STATE_NEGOTIATED] = negotiated, [DIAGNOSTIC_IOP_SACK_STATE_UNUSED2] = 0, [DIAGNOSTIC_IOP_SACK_STATE_UNUSED3] = 0, [DIAGNOSTIC_IOP_SACK_STATE_UNUSED4] = 0, [DIAGNOSTIC_IOP_SACK_STATE_UNUSED5] = 0 });

        sack_seen       = 1;
        sack_negotiated = negotiated;
        sack_at         = now;
    }
}

/**
 * @brief Decode an unaligned network-order option word.
 * @param bytes Four available bytes.
 * @return Host-order value.
 */
static uint32_t option_word(const unsigned char* bytes)
{
    uint32_t value;

    memcpy(&value, bytes, sizeof(value));

    return lwip_ntohl(value);
}

/**
 * @brief Record actual SACK options without reading beyond the contiguous header.
 * @param header Outgoing TCP header.
 * @param available Contiguous bytes sampled before IP output changes the pbuf.
 * @param result IP submission result, not proof of delivery.
 */
static void sack_options(const struct tcp_hdr* header, unsigned available, int result)
{
    unsigned             length = TCPH_HDRLEN_BYTES(header);
    unsigned             count  = 0;
    unsigned             valid  = length >= TCP_HLEN && length <= DIAGNOSTIC_TCP_MAX_HEADER && length <= available;
    uint32_t             blocks[DIAGNOSTIC_TCP_SACK_BLOCKS][2];
    const unsigned char* bytes = (const unsigned char*)header;

    for (unsigned at = TCP_HLEN; valid && at < length;)
    {
        unsigned kind = bytes[at];

        if (kind == DIAGNOSTIC_TCP_OPTION_END)
        {
            break;
        }

        if (kind == DIAGNOSTIC_TCP_OPTION_NOP)
        {
            ++at;
            continue;
        }

        if (length - at < DIAGNOSTIC_TCP_OPTION_PREFIX)
        {
            valid = 0;

            break;
        }

        unsigned size = bytes[at + 1];

        if (size < DIAGNOSTIC_TCP_OPTION_PREFIX || size > length - at)
        {
            valid = 0;

            break;
        }

        if (kind == DIAGNOSTIC_TCP_OPTION_SACK)
        {
            unsigned n = (size - DIAGNOSTIC_TCP_OPTION_PREFIX) / DIAGNOSTIC_TCP_SACK_BYTES;

            if (count || !n || n > DIAGNOSTIC_TCP_SACK_BLOCKS || (size - DIAGNOSTIC_TCP_OPTION_PREFIX) % DIAGNOSTIC_TCP_SACK_BYTES)
            {
                valid = 0;

                break;
            }

            for (unsigned i = 0; i < n; ++i)
            {
                const unsigned char* block = bytes + at + DIAGNOSTIC_TCP_OPTION_PREFIX + i * DIAGNOSTIC_TCP_SACK_BYTES;

                blocks[i][0] = option_word(block);
                blocks[i][1] = option_word(block + sizeof(uint32_t));
            }

            count = n;
        }

        at += size;
    }

    if (count || !valid)
    {
        uint32_t ack = lwip_ntohl(header->ackno);

        record(DIAGNOSTIC_IOP_SACK_PACKET, (const uint32_t[DIAGNOSTIC_IOP_DATA_WORDS]){ [DIAGNOSTIC_IOP_SACK_PACKET_TX_ID] = tx_serial, [DIAGNOSTIC_IOP_SACK_PACKET_ACK] = ack, [DIAGNOSTIC_IOP_SACK_PACKET_BLOCK_COUNT] = valid ? count : 0, [DIAGNOSTIC_IOP_SACK_PACKET_VALID] = valid, [DIAGNOSTIC_IOP_SACK_PACKET_HEADER_BYTES] = length, [DIAGNOSTIC_IOP_SACK_PACKET_RESULT] = (uint32_t)result });

        for (unsigned i = 0; valid && i < count; ++i)
        {
            record(DIAGNOSTIC_IOP_SACK_BLOCK, (const uint32_t[DIAGNOSTIC_IOP_DATA_WORDS]){ [DIAGNOSTIC_IOP_SACK_BLOCK_TX_ID] = tx_serial, [DIAGNOSTIC_IOP_SACK_BLOCK_ACK] = ack, [DIAGNOSTIC_IOP_SACK_BLOCK_INDEX] = i, [DIAGNOSTIC_IOP_SACK_BLOCK_LEFT] = blocks[i][0], [DIAGNOSTIC_IOP_SACK_BLOCK_RIGHT] = blocks[i][1], [DIAGNOSTIC_IOP_SACK_BLOCK_RESULT] = (uint32_t)result });
        }
    }
}

void platform_net_diagnostic_rx(const struct tcp_pcb* pcb, uint32_t seq, uint32_t ack, unsigned length, unsigned window, unsigned flags)
{
    int state;

    CpuSuspendIntr(&state);

    if (selected(pcb))
    {
        sack_state(pcb);
        record(DIAGNOSTIC_IOP_RX, (const uint32_t[DIAGNOSTIC_IOP_DATA_WORDS]){ [DIAGNOSTIC_IOP_RX_SEQUENCE] = seq, [DIAGNOSTIC_IOP_RX_ACK] = ack, [DIAGNOSTIC_IOP_RX_PAYLOAD_BYTES] = length, [DIAGNOSTIC_IOP_RX_PEER_WINDOW] = window, [DIAGNOSTIC_IOP_RX_NEXT_EXPECTED] = pcb->rcv_nxt, [DIAGNOSTIC_IOP_RX_FLAGS] = flags });
    }

    CpuResumeIntr(state);
}

void platform_net_diagnostic_tx(const struct tcp_pcb* pcb, const struct tcp_hdr* header, unsigned header_bytes, int result)
{
    int state;

    CpuSuspendIntr(&state);

    if (selected(pcb))
    {
        sack_state(pcb);

        if (!header || header_bytes < TCP_HLEN)
        {
            CpuResumeIntr(state);
            return;
        }

        ++tx_serial;
        record(DIAGNOSTIC_IOP_TX, (const uint32_t[DIAGNOSTIC_IOP_DATA_WORDS]){ [DIAGNOSTIC_IOP_TX_SEQUENCE] = lwip_ntohl(header->seqno), [DIAGNOSTIC_IOP_TX_ACK] = lwip_ntohl(header->ackno), [DIAGNOSTIC_IOP_TX_ADVERTISED_WINDOW] = lwip_ntohs(header->wnd), [DIAGNOSTIC_IOP_TX_INTERNAL_WINDOW] = pcb->rcv_wnd, [DIAGNOSTIC_IOP_TX_FLAGS] = TCPH_FLAGS(header), [DIAGNOSTIC_IOP_TX_RESULT] = (uint32_t)result });
        sack_options(header, header_bytes, result);
    }

    CpuResumeIntr(state);
}

void platform_net_diagnostic_delivery(const struct tcp_pcb* pcb, unsigned length, int result)
{
    int state;

    CpuSuspendIntr(&state);

    if (selected(pcb))
    {
        if (!result)
        {
            delivered_bytes += length;
        }

        record(DIAGNOSTIC_IOP_DELIVER, (const uint32_t[DIAGNOSTIC_IOP_DATA_WORDS]){ [DIAGNOSTIC_IOP_DELIVER_BYTES] = length, [DIAGNOSTIC_IOP_DELIVER_RESULT] = (uint32_t)result, [DIAGNOSTIC_IOP_DELIVER_DELIVERED_BYTES] = delivered_bytes, [DIAGNOSTIC_IOP_DELIVER_NEXT_EXPECTED] = pcb->rcv_nxt, [DIAGNOSTIC_IOP_DELIVER_INTERNAL_WINDOW] = pcb->rcv_wnd, [DIAGNOSTIC_IOP_DELIVER_UNUSED] = 0 });
    }

    CpuResumeIntr(state);
}

void platform_net_diagnostic_failure(unsigned kind, int value)
{
    static const struct
    {
        unsigned kind, field;
    } failures[] = {
        { DIAGNOSTIC_IOP_INPUT_ERROR, DIAGNOSTIC_IOP_INPUT_ERROR_ERROR },
        { DIAGNOSTIC_IOP_POOL_ERROR, DIAGNOSTIC_IOP_POOL_ERROR_POOL },
        { DIAGNOSTIC_IOP_HEAP_ERROR, DIAGNOSTIC_IOP_HEAP_ERROR_REQUESTED_BYTES },
    };

    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i)
    {
        if (failures[i].kind == kind)
        {
            uint32_t data[DIAGNOSTIC_IOP_DATA_WORDS] = { 0 };

            data[failures[i].field] = (uint32_t)value;
            record(kind, data);
            return;
        }
    }
}

void platform_net_diagnostic_service(const Ps2DiagnosticRequest* request, Ps2DiagnosticReply* reply)
{
    if (request->abi != DIAGNOSTIC_IOP_ABI)
    {
        return;
    }

    if (request->command == DIAGNOSTIC_IOP_READ_BEGIN || request->command == DIAGNOSTIC_IOP_READ_END)
    {
        int state;

        CpuSuspendIntr(&state);

        if (request->socket == selected_socket)
        {
            if (request->command == DIAGNOSTIC_IOP_READ_END && request->result > 0)
            {
                read_bytes += (unsigned)request->result;
            }

            if (request->command == DIAGNOSTIC_IOP_READ_BEGIN)
            {
                record(DIAGNOSTIC_IOP_BEGIN, (const uint32_t[DIAGNOSTIC_IOP_DATA_WORDS]){ [DIAGNOSTIC_IOP_BEGIN_UNUSED] = (uint32_t)request->result, [DIAGNOSTIC_IOP_BEGIN_READ_BYTES] = read_bytes });
            }
            else
            {
                record(DIAGNOSTIC_IOP_READ, (const uint32_t[DIAGNOSTIC_IOP_DATA_WORDS]){ [DIAGNOSTIC_IOP_READ_RESULT] = (uint32_t)request->result, [DIAGNOSTIC_IOP_READ_READ_BYTES] = read_bytes });
            }
        }

        CpuResumeIntr(state);
        return;
    }

    memset(reply, 0, sizeof(*reply));

    reply->abi = DIAGNOSTIC_IOP_ABI;

    if (request->command == DIAGNOSTIC_IOP_SELECT)
    {
        struct sockaddr_in peer;
        socklen_t          length = sizeof(peer);

        memset(&peer, 0, sizeof(peer));

        if (request->socket >= 0 && lwip_getpeername(request->socket, (struct sockaddr*)&peer, &length) < 0)
        {
            int state;

            CpuSuspendIntr(&state);

            selected_socket = -1;
            record_count    = 0;

            CpuResumeIntr(state);

            reply->status = PS2_DIAGNOSTIC_ERROR_SOCKET;

            return;
        }

        int state;

        CpuSuspendIntr(&state);

        sack_seen       = 0;
        sack_negotiated = sack_at = tx_serial = 0;
        selected_socket                       = request->socket;
        peer_address                          = lwip_ntohl(peer.sin_addr.s_addr);
        peer_port                             = lwip_ntohs(peer.sin_port);
        write_index = record_count = lost_records = delivered_bytes = read_bytes = 0;

        CpuResumeIntr(state);
    }

    int state;

    CpuSuspendIntr(&state);

    reply->status = 1;
    reply->now    = sys_now();
    reply->lost   = lost_records;
    reply->peer   = peer_address;
    reply->port   = peer_port;

    while (record_count && reply->count < DIAGNOSTIC_IOP_BATCH)
    {
        reply->records[reply->count++] = history[(write_index + DIAGNOSTIC_IOP_RING - record_count) % DIAGNOSTIC_IOP_RING];

        --record_count;
    }

    CpuResumeIntr(state);
}
