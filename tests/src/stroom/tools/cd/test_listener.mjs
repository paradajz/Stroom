import assert from "node:assert/strict";
import dgram from "node:dgram";
import {
  audio,
  cdStream as wire,
} from "../../../../../tools/contracts/load.mjs";
import {
  discoveryBroadcasts,
  discoverCdSources,
  queryCdTransport,
  formatCdTransport,
} from "../../../../../tools/cd/listener/stream.mjs";

const traceSocket = dgram.createSocket("udp4");
await new Promise((resolve) => traceSocket.bind(0, "127.0.0.1", resolve));
const tcpFields = [
  "flags",
  "state",
  "last_ack",
  "next_sequence",
  "last_written",
  "send_space",
  "queue_length",
  "unsent_segments",
  "unsent_bytes",
  "unsent_ref",
  "unacked_segments",
  "unacked_bytes",
  "unacked_ref",
  "congestion_window",
  "peer_window",
  "retransmissions",
  "rto_ms",
  "persist_backoff",
];
const traceReply = {
  service: wire.CD_STREAM_SERVICE,
  traceVersion: 1,
  id: 2,
  duration_ms: 1480,
  sample_ms: 1000,
  lag: 75,
  available: 1,
  tcp: {
    ...Object.fromEntries(tcpFields.map((key) => [key, 0])),
    unacked_bytes: 5840,
    unacked_segments: 4,
    timer_ms: -1,
  },
};
let malformed = false;
traceSocket.on("message", (data, peer) => {
  assert.equal(data.toString(), wire.CD_STREAM_TRACE);
  const invalid = { ...traceReply, tcp: { ...traceReply.tcp, send_space: -1 } };
  traceSocket.send(
    Buffer.from(JSON.stringify(invalid)),
    peer.port,
    peer.address,
  );
  if (!malformed)
    traceSocket.send(
      Buffer.from(JSON.stringify(traceReply)),
      peer.port,
      peer.address,
    );
});
try {
  const source = { host: "127.0.0.1", port: traceSocket.address().port };
  const trace = await queryCdTransport(source);
  assert.deepEqual(trace, traceReply);
  assert.match(formatCdTransport(trace), /stall #2: duration 1480 ms/);
  assert.match(formatCdTransport(trace), /unacked 5840 bytes\/4 segments/);
  traceReply.driver = {
    status: 1,
    link_mode: 4,
    tx_dropped: 2,
    tx_errors: 2,
    tx_carrier: 0,
    tx_deferred: 0,
    tx_collisions: 0,
    tx_underruns: 2,
    rx_alloc_fail: 0,
    rx_overrun: 0,
    rx_bad_fcs: 0,
  };
  const withDriver = await queryCdTransport(source);
  assert.deepEqual(withDriver.driver, traceReply.driver);
  assert.match(formatCdTransport(withDriver), /TX dropped 2; errors 2/);
  assert.match(formatCdTransport(withDriver), /underruns 2/);
  assert.match(
    formatCdTransport({
      ...withDriver,
      driver: { ...traceReply.driver, status: 0 },
    }),
    /transmit counters unavailable/,
  );
  traceReply.driver.tx_dropped = -1;
  assert.equal(await queryCdTransport(source, { timeout: 20 }), null);
  delete traceReply.driver;
  assert.match(
    formatCdTransport({ ...trace, available: 0 }),
    /diagnostics unavailable/,
  );
  malformed = true;
  assert.equal(await queryCdTransport(source, { timeout: 20 }), null);
  const stopped = new AbortController();
  stopped.abort();
  assert.equal(
    await queryCdTransport(source, { signal: stopped.signal }),
    null,
  );
} finally {
  traceSocket.close();
}

assert.deepEqual(discoveryBroadcasts({}), ["255.255.255.255"]);
assert.deepEqual(
  discoveryBroadcasts({
    lan: [
      {
        family: "IPv4",
        address: "192.168.1.174",
        netmask: "255.255.255.0",
        internal: false,
      },
      {
        family: "IPv4",
        address: "192.168.1.175",
        netmask: "255.255.255.0",
        internal: false,
      },
      {
        family: "IPv6",
        address: "fe80::1",
        netmask: "ffff:ffff:ffff:ffff::",
        internal: false,
      },
    ],
    docker: [
      {
        family: 4,
        address: "172.17.0.2",
        netmask: "255.255.0.0",
        internal: false,
      },
    ],
    other: [
      {
        family: "IPv4",
        address: "10.4.2.9",
        netmask: "255.255.254.0",
        internal: false,
      },
    ],
    loopback: [
      {
        family: "IPv4",
        address: "127.0.0.1",
        netmask: "255.0.0.0",
        internal: true,
      },
    ],
    vpn: [
      {
        family: "IPv4",
        address: "10.8.0.1",
        netmask: "255.255.255.255",
        internal: false,
      },
    ],
    point: [
      {
        family: "IPv4",
        address: "10.9.0.0",
        netmask: "255.255.255.254",
        internal: false,
      },
    ],
    invalid: [
      {
        family: "IPv4",
        address: "10.0.0.1",
        netmask: "255.0.255.0",
        internal: false,
      },
    ],
  }),
  ["10.4.3.255", "172.17.255.255", "192.168.1.255", "255.255.255.255"],
);

const discoveryPeer = dgram.createSocket("udp4");
await new Promise((resolve) => discoveryPeer.bind(0, "127.0.0.1", resolve));
let discoveryReply;
discoveryPeer.on("message", (_query, peer) => {
  if (discoveryReply)
    discoveryPeer.send(JSON.stringify(discoveryReply), peer.port, peer.address);
  for (const reply of [
    "null",
    "[]",
    "42",
    JSON.stringify({ service: wire.CD_STREAM_SERVICE, version: 99 }),
  ])
    discoveryPeer.send(reply, peer.port, peer.address);
});
try {
  assert.deepEqual(
    await discoverCdSources({
      broadcast: "127.0.0.1",
      port: discoveryPeer.address().port,
      timeout: 50,
    }),
    [],
  );
  discoveryReply = {
    service: wire.CD_STREAM_SERVICE,
    version: wire.CD_STREAM_VERSION,
    port: discoveryPeer.address().port,
    sample_rate: audio.AUDIO_RATE,
    channels: 2,
    bits: 16,
    stats: {
      disconnects: 2,
      send_retries: 60,
      resets: 0,
      mailbox_overflows: 0,
      max_lag_blocks: 59,
      max_poll_gap_ms: 12,
      last_disconnect: "send_stall",
      last_error: 11,
    },
  };
  discoveryReply.stats.source = {
    blocks: 75,
    gaps: 1,
    max_gap_ms: 60,
    drive_waits: 2,
    output_busy: 8,
    silence_blocks: 1,
    max_drive_wait_ms: 20,
    max_output_busy_ms: 50,
  };
  const statsSource = (
    await discoverCdSources({
      broadcast: "127.0.0.1",
      port: discoveryPeer.address().port,
      timeout: 50,
      includeStats: true,
    })
  )[0];
  assert.deepEqual(statsSource.stats, discoveryReply.stats);
  discoveryReply.stats.last_disconnect = "untrusted\nmessage";
  const invalidStats = (
    await discoverCdSources({
      broadcast: "127.0.0.1",
      port: discoveryPeer.address().port,
      timeout: 50,
      includeStats: true,
    })
  )[0];
  assert.equal(invalidStats.stats, undefined);
} finally {
  discoveryPeer.close();
}

console.log("CD diagnostic discovery and source counters passed");
