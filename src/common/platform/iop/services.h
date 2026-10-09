#pragma once

/* PS2SDK RPC service IDs. CD and pad IDs are private to the SDK sources.
 * See ee/rpc/cdvd/src/{libcdvd,ncmd,scmd}.c and ee/rpc/pad/src/libpad.c. */
#define PS2_RPC_CDVD_INIT 0x80000592u
#define PS2_RPC_CDVD_SCMD 0x80000593u
#define PS2_RPC_CDVD_NCMD 0x80000595u
#define PS2_RPC_PAD_NEW_1 0x80000100u
#define PS2_RPC_PAD_NEW_2 0x80000101u
#define PS2_RPC_PAD_OLD_1 0x8000010fu
#define PS2_RPC_PAD_OLD_2 0x8000011fu
#define PS2_RPC_SOCKET    0x5053564eu /* Private stroom socket bridge; preserve launcher RPC. */

/* Startup probes retry for roughly one second per required service. */
#define PS2_RPC_PROBE_ATTEMPTS 100
#define PS2_RPC_PROBE_DELAY_US 10000
#define PS2_RPC_ALIGNMENT      64
