import assert from "node:assert/strict";
import dgram from "node:dgram";
import net from "node:net";
import { Readable } from "node:stream";
import { spawn, spawnSync } from "node:child_process";
import { once } from "node:events";
import {
  mkdtempSync,
  writeFileSync,
  readFileSync,
  rmSync,
  existsSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { PC_BUFFER } from "../../../../../tools/cd/playback.mjs";
import {
  audio,
  cdStream as wire,
} from "../../../../../tools/contracts/load.mjs";
import {
  decodeCdStream,
  discoveryBroadcasts,
  discoverCdSources,
  receiveCdStream,
  queryCdTransport,
  formatCdTransport,
} from "../../../../../tools/cd/stream.mjs";

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

function record(type, data = Buffer.alloc(0)) {
  const header = Buffer.alloc(wire.CD_STREAM_HEADER_BYTES);
  header.write(wire.CD_STREAM_MAGIC);
  header.writeUInt32BE(type, 4);
  header.writeUInt32BE(data.length, 8);
  return Buffer.concat([header, data]);
}
const format = Buffer.alloc(12);
format.writeUInt32BE(audio.AUDIO_RATE, 0);
format.writeUInt32BE(2, 4);
format.writeUInt32BE(16, 8);
// Independent v1 wire bytes catch accidental protocol changes.
assert.deepEqual(
  record(wire.CD_STREAM_FORMAT, format),
  Buffer.from("53434431000000020000000c0000bb800000000200000010", "hex"),
);
assert.equal(wire.CD_STREAM_REQUEST, "STROOM CD 1\n");
const expected = Buffer.alloc(wire.CD_STREAM_MAX_PAYLOAD_BYTES);
for (let i = 0; i < expected.length; ++i) expected[i] = (i * 37) & 255;
const frames = Buffer.concat([
  record(wire.CD_STREAM_FORMAT, format),
  record(wire.CD_STREAM_PCM, expected),
  record(wire.CD_STREAM_END),
]);
async function decode(bytes, step = 7) {
  const chunks = [];
  for (let i = 0; i < bytes.length; i += step)
    chunks.push(bytes.subarray(i, i + step));
  const result = [];
  for await (const item of decodeCdStream(Readable.from(chunks)))
    result.push(item);
  return result;
}
for (const size of [1, 7, 12, 2560, 65536]) {
  const decoded = await decode(frames, size);
  assert.deepEqual(
    decoded.map((item) => item.type),
    ["format", "pcm", "end"],
  );
  assert.deepEqual(decoded[1].data, expected);
}
await assert.rejects(decode(frames.subarray(0, -1)), /Truncated/);
await assert.rejects(
  decode(record(wire.CD_STREAM_PCM, expected)),
  /Unexpected/,
);
await assert.rejects(
  decode(record(wire.CD_STREAM_FORMAT, Buffer.alloc(12))),
  /Unsupported/,
);
await assert.rejects(
  decode(
    Buffer.concat([
      record(wire.CD_STREAM_FORMAT, format),
      record(wire.CD_STREAM_FORMAT, format),
    ]),
  ),
  /Unexpected/,
);
const oversized = record(wire.CD_STREAM_PCM);
oversized.writeUInt32BE(0xffffffff, 8);
await assert.rejects(decode(oversized), /size limit/);
const highBitMagic = Buffer.from(frames);
highBitMagic[0] |= 0x80;
await assert.rejects(decode(highBitMagic), /Invalid/);
const invalid = Buffer.from(frames);
invalid[0] = 0;
await assert.rejects(decode(invalid), /Invalid/);
await assert.rejects(
  decode(record(wire.CD_STREAM_FORMAT, format)),
  /interrupted/,
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

const cli = new URL("../../../../../tools/cd/listener.mjs", import.meta.url);
const help = spawnSync(process.execPath, [cli.pathname, "--help"], {
  encoding: "utf8",
});
assert.equal(help.status, 0);
assert.ok(
  help.stdout.includes(
    `${PC_BUFFER.minMs}–${PC_BUFFER.maxMs} ms (default: ${PC_BUFFER.defaultMs})`,
  ),
);
assert.equal(
  spawnSync(process.execPath, [cli.pathname, "--port", "0"]).status,
  1,
);
assert.equal(
  spawnSync(process.execPath, [cli.pathname, "--host", ""]).status,
  1,
);
for (const value of [
  String(PC_BUFFER.minMs - 1),
  String(PC_BUFFER.maxMs + 1),
  "nope",
  "",
  "1.5",
]) {
  const result = spawnSync(
    process.execPath,
    [cli.pathname, `--buffer-ms=${value}`],
    { encoding: "utf8" },
  );
  assert.equal(result.status, 1);
  assert.ok(
    result.stderr.startsWith(
      `PC buffer must be between ${PC_BUFFER.minMs} and ${PC_BUFFER.maxMs} milliseconds\n`,
    ),
  );
}
for (const value of [PC_BUFFER.minMs, PC_BUFFER.defaultMs, PC_BUFFER.maxMs])
  assert.equal(
    spawnSync(process.execPath, [
      cli.pathname,
      "--buffer-ms",
      String(value),
      "--help",
    ]).status,
    0,
  );

const unavailablePort = net.createServer();
await new Promise((resolve) => unavailablePort.listen(0, "127.0.0.1", resolve));
const closedPort = unavailablePort.address().port;
await new Promise((resolve) => unavailablePort.close(resolve));
const waitingListener = spawn(
  process.execPath,
  [cli.pathname, "--host", "127.0.0.1", "--port", String(closedPort)],
  { stdio: ["ignore", "ignore", "pipe"] },
);
let waitingOutput = "";
waitingListener.stderr.on("data", (data) => {
  waitingOutput += data;
});
const waitingExit = once(waitingListener, "close");
try {
  await new Promise((resolve) => setTimeout(resolve, 2250));
} finally {
  waitingListener.kill("SIGTERM");
  await waitingExit;
}
assert.equal(waitingOutput.trim().split("\n").length, 1, waitingOutput);
assert.match(waitingOutput, /Waiting for PS2/);
assert.doesNotMatch(waitingOutput, /Listening to|ECONNREFUSED/);

const directory = mkdtempSync(join(tmpdir(), "stroom-listener-"));
const fixture = spawn(process.argv[2], [], {
  stdio: ["pipe", "pipe", "inherit"],
});
let lines = [],
  waiters = [];
let observedClients = 0;
const clientWaiters = [];
let buffered = "";
fixture.stdout.on("data", (chunk) => {
  buffered += chunk;
  for (;;) {
    const end = buffered.indexOf("\n");
    if (end < 0) break;
    const line = buffered.slice(0, end);
    buffered = buffered.slice(end + 1);
    const clients = /^CLIENTS (\d+)$/.exec(line);
    if (clients) {
      observedClients = Number(clients[1]);
      for (let i = clientWaiters.length - 1; i >= 0; --i) {
        if (clientWaiters[i].count === observedClients)
          clientWaiters.splice(i, 1)[0].resolve();
      }
      continue;
    }
    const index = waiters.findIndex((waiter) => waiter.line === line);
    if (index >= 0) waiters.splice(index, 1)[0].resolve();
    else lines.push(line);
  }
});
function line(value) {
  const clients = /^CLIENTS (\d+)$/.exec(value);
  if (clients) {
    const count = Number(clients[1]);
    if (observedClients === count) return Promise.resolve();
    return new Promise((resolve) => clientWaiters.push({ count, resolve }));
  }
  const index = lines.indexOf(value);
  if (index >= 0) {
    lines.splice(index, 1);
    return Promise.resolve();
  }
  return new Promise((resolve) => waiters.push({ line: value, resolve }));
}
let child;
const deadline = setTimeout(() => {
  fixture.kill("SIGKILL");
  child?.kill("SIGKILL");
  throw Error("Listener integration timed out");
}, 25000);
try {
  await line("READY");
  const sources = await discoverCdSources({
    broadcast: "127.0.0.1",
    timeout: 100,
  });
  assert.deepEqual(sources, [
    { host: "127.0.0.1", port: wire.CD_STREAM_PORT, name: "Stroom CD" },
  ]);
  let ready = 0,
    release;
  const bothReady = new Promise((resolve) => {
    release = resolve;
  });
  async function collect() {
    const pcm = [];
    for await (const item of receiveCdStream(sources[0])) {
      if (item.type === "format") {
        if (++ready === 2) release();
        await bothReady;
      }
      if (item.type === "pcm") pcm.push(item.data);
    }
    return Buffer.concat(pcm);
  }
  const first = collect(),
    second = collect();
  await bothReady;
  await line("CLIENTS 2");
  fixture.stdin.write("PPE");
  assert.deepEqual(await first, Buffer.concat([expected, expected]));
  assert.deepEqual(await second, Buffer.concat([expected, expected]));
  await line("CLIENTS 0");
  fixture.stdin.write("R");
  await line("RESET");

  // Exercise the actual CLI, discovery and playback subprocess with deterministic PCM.
  const output = join(directory, "received.pcm");
  const player = join(directory, "player.cjs");
  writeFileSync(
    player,
    "#!" +
      process.execPath +
      `
const fs = require('node:fs');
const output = process.env.STROOM_TEST_PCM;
const take = suffix => {
  if (!fs.existsSync(output + suffix)) return false;
  fs.unlinkSync(output + suffix);
  return true;
};
const hang = take('.hang-once');
if (hang) process.on('SIGTERM', () => {});
fs.writeFileSync(output + '.started', '');
if (take('.exit-once')) setTimeout(() => process.exit(0), 25);
const chunks = [];
let bytes = 0;
process.stdin.on('data', c => {
  chunks.push(c);
  bytes += c.length;
  fs.writeFileSync(output + '.progress', String(bytes));
});
process.stdin.on('end', () => {
  fs.writeFileSync(output, Buffer.concat(chunks));
  if (hang) setInterval(() => {}, 1000);
});
`,
    { mode: 0o755 },
  );
  // CD autoplay can submit its opening before discovery/subscription completes.
  // A small PC reserve must still deliver those exact samples to the player.
  const openingBlocks = 12;
  fixture.stdin.write("P".repeat(openingBlocks) + "B");
  await line("BARRIER");
  child = spawn(
    process.execPath,
    [
      cli.pathname,
      "--broadcast",
      "127.0.0.1",
      "--once",
      "--stats",
      "--buffer-ms",
      "50",
      "--ffplay",
      player,
    ],
    {
      env: { ...process.env, STROOM_TEST_PCM: output },
      stdio: ["ignore", "ignore", "pipe"],
    },
  );
  let errors = "";
  child.stderr.on("data", (data) => {
    errors += data;
  });
  const complete = once(child, "close");
  await line("CLIENTS 1");
  fixture.stdin.write("PPE");
  assert.equal((await complete)[0], 0, errors);
  const openingAudio = Buffer.concat(Array(openingBlocks + 2).fill(expected));
  assert.deepEqual(readFileSync(output), openingAudio);
  assert.ok(
    errors.includes(
      `CD timing: received ${(openingAudio.length / (audio.AUDIO_RATE * 4)).toFixed(3)} s audio`,
    ),
    errors,
  );
  child = undefined;
  await line("CLIENTS 0");
  fixture.stdin.write("R");
  await line("RESET");

  // Permanent local player setup errors exit after a single subscription.
  writeFileSync(join(directory, "denied-player"), "#!/bin/sh\nexit 0\n", {
    mode: 0o644,
  });
  for (const [name, code] of [
    ["missing-player", "ENOENT"],
    ["denied-player", "EACCES"],
    ["denied-player/ffplay", "ENOTDIR"],
  ]) {
    const executable = join(directory, name);
    child = spawn(
      process.execPath,
      [
        cli.pathname,
        "--host",
        "127.0.0.1",
        "--buffer-ms",
        "0",
        "--ffplay",
        executable,
      ],
      { stdio: ["ignore", "ignore", "pipe"] },
    );
    let playerError = "";
    child.stderr.on("data", (data) => {
      playerError += data;
    });
    const playerExit = once(child, "close");
    await line("CLIENTS 1");
    fixture.stdin.write("P");
    assert.equal((await playerExit)[0], 1, playerError);
    assert.ok(
      playerError.includes(`Cannot start FFplay "${executable}"`),
      playerError,
    );
    assert.ok(playerError.includes(code), playerError);
    assert.equal(
      (playerError.match(/Connected to /g) ?? []).length,
      1,
      playerError,
    );
    child = undefined;
    await line("CLIENTS 0");
    fixture.stdin.write("R");
    await line("RESET");
  }

  // Exhausted playback refills on the same connection and player, preserving PCM.
  const progress = output + ".progress";
  rmSync(output);
  rmSync(output + ".started");
  rmSync(progress);
  child = spawn(
    process.execPath,
    [
      cli.pathname,
      "--host",
      "127.0.0.1",
      "--once",
      "--buffer-ms",
      "40",
      "--ffplay",
      player,
    ],
    {
      env: { ...process.env, STROOM_TEST_PCM: output },
      stdio: ["ignore", "ignore", "pipe"],
    },
  );
  let recoveryLog = "";
  child.stderr.on("data", (data) => {
    recoveryLog += data;
  });
  const recoveryExit = once(child, "close");
  const bytesReceived = () =>
    existsSync(progress) ? Number(readFileSync(progress, "utf8")) : 0;
  async function waitUntil(condition, message) {
    const deadline = Date.now() + 2000;
    while (!condition()) {
      assert(Date.now() < deadline, message + "\n" + recoveryLog);
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
  }
  await line("CLIENTS 1");
  fixture.stdin.write("PPPP");
  await waitUntil(
    () => bytesReceived() === expected.length * 4,
    "Initial PCM must reach the player",
  );
  await new Promise((resolve) => setTimeout(resolve, 150));
  fixture.stdin.write("P");
  await waitUntil(
    () => recoveryLog.includes("Rebuffering 40 ms"),
    "The listener must detect reserve exhaustion",
  );
  assert.equal(bytesReceived(), expected.length * 4);
  fixture.stdin.write("P");
  await new Promise((resolve) => setTimeout(resolve, 25));
  assert.equal(
    bytesReceived(),
    expected.length * 4,
    "A partial recovery reserve must stay buffered",
  );
  fixture.stdin.write("P");
  await waitUntil(
    () => bytesReceived() === expected.length * 7,
    "Refilling must resume the same player",
  );
  assert.match(recoveryLog, /CD audio ready; resuming playback/);
  fixture.stdin.write("E");
  assert.equal((await recoveryExit)[0], 0, recoveryLog);
  assert.deepEqual(
    readFileSync(output),
    Buffer.concat(Array(7).fill(expected)),
  );
  assert.equal(
    (recoveryLog.match(/Connected to /g) ?? []).length,
    1,
    recoveryLog,
  );
  assert.equal(
    (recoveryLog.match(/Listening to /g) ?? []).length,
    1,
    recoveryLog,
  );
  child = undefined;
  await line("CLIENTS 0");
  fixture.stdin.write("R");
  await line("RESET");

  // Default mode flushes a cancelled player and automatically reconnects.
  rmSync(output);
  rmSync(output + ".started");
  child = spawn(
    process.execPath,
    [cli.pathname, "--broadcast", "127.0.0.1", "--ffplay", player],
    {
      env: { ...process.env, STROOM_TEST_PCM: output },
      stdio: ["ignore", "ignore", "pipe"],
    },
  );
  child.stderr.on("data", (data) => {
    errors += data;
  });
  const reconnectedExit = once(child, "close");
  await line("CLIENTS 1");
  fixture.stdin.write("P".repeat(40));
  async function waitForFile(file) {
    while (!existsSync(file))
      await new Promise((resolve) => setTimeout(resolve, 10));
  }
  await waitForFile(output + ".started");
  assert.equal(existsSync(output), false);
  fixture.stdin.write("R");
  await line("RESET");
  await line("CLIENTS 0");
  await line("CLIENTS 1");
  assert.match(
    errors,
    /CD playback interrupted \(PS2: playback_reset; errno 0;/,
  );
  assert.equal(
    existsSync(output),
    false,
    "Cancelled playback must not drain stale PCM",
  );
  fixture.stdin.write("PPE");
  await waitForFile(output);
  assert.deepEqual(readFileSync(output), Buffer.concat([expected, expected]));

  // The same listener follows repeated clean endings without being restarted.
  for (let session = 0; session < 2; ++session) {
    await line("CLIENTS 0");
    await line("CLIENTS 1");
    rmSync(output);
    fixture.stdin.write("PPE");
    await waitForFile(output);
    assert.deepEqual(readFileSync(output), Buffer.concat([expected, expected]));
  }

  await line("CLIENTS 0");
  await line("CLIENTS 1");
  fixture.stdin.write("S");
  await line("OFFLINE");
  await line("CLIENTS 0");
  await new Promise((resolve) => setTimeout(resolve, 1300));
  assert.equal(
    child.exitCode,
    null,
    "Listener must survive an offline console",
  );
  fixture.stdin.write("O");
  await line("ONLINE");
  await line("CLIENTS 1");
  rmSync(output);
  fixture.stdin.write("PPE");
  await waitForFile(output);
  assert.deepEqual(readFileSync(output), Buffer.concat([expected, expected]));

  // A local player exiting while the remote stream waits must cancel that
  // subscription immediately, without requiring another PCM packet to arrive.
  await line("CLIENTS 0");
  await line("CLIENTS 1");
  rmSync(output);
  rmSync(output + ".started");
  writeFileSync(output + ".exit-once", "");
  fixture.stdin.write("P".repeat(40));
  await waitForFile(output + ".started");
  await line("CLIENTS 0");
  await line("CLIENTS 1");
  assert.match(errors, /FFplay exited before the stream ended/);
  fixture.stdin.write("PPE");
  await waitForFile(output);
  assert.deepEqual(readFileSync(output), Buffer.concat([expected, expected]));

  // A player that ignores SIGTERM after END cannot prevent the next session.
  await line("CLIENTS 0");
  await line("CLIENTS 1");
  rmSync(output);
  writeFileSync(output + ".hang-once", "");
  fixture.stdin.write("PPE");
  await waitForFile(output);
  await line("CLIENTS 0");
  await line("CLIENTS 1");
  assert.match(errors, /FFplay did not finish playback/);
  rmSync(output);
  fixture.stdin.write("PPE");
  await waitForFile(output);
  assert.deepEqual(readFileSync(output), Buffer.concat([expected, expected]));

  child.kill("SIGTERM");
  assert.equal((await reconnectedExit)[0], 143, errors);
  child = undefined;
  await line("CLIENTS 0");
  fixture.stdin.write("R");
  await line("RESET");

  // A discontinuity has no END record: the receiver must reject it.
  await assert.rejects(
    (async () => {
      for await (const item of receiveCdStream(sources[0])) {
        if (item.type === "format") fixture.stdin.write("R");
      }
    })(),
    /interrupted/,
  );
  await line("RESET");
  console.log("CD discovery, framing, two listeners and PC playback passed");
} finally {
  clearTimeout(deadline);
  child?.kill("SIGKILL");
  fixture.stdin.end("Q");
  await once(fixture, "close");
  rmSync(directory, { recursive: true, force: true });
}
