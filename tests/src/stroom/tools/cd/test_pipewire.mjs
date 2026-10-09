import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import {
  mkdtemp,
  writeFile,
  readFile,
  readdir,
  access,
  stat,
  chmod,
  rm,
} from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import net from "node:net";
import {
  audio,
  cdStream as wire,
} from "../../../../../tools/contracts/load.mjs";

const [binary, pipewire, link, captureProgram] = process.argv.slice(2);
const captureSamples = captureProgram && !captureProgram.endsWith("-NOTFOUND");
const directory = await mkdtemp(join(tmpdir(), "stroom-pipewire-"));
await chmod(directory, 0o700);
const children = new Set();
const sockets = new Set();
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const environment = {
  ...process.env,
  XDG_CONFIG_HOME: directory,
  XDG_RUNTIME_DIR: directory,
  PIPEWIRE_RUNTIME_DIR: directory,
  PIPEWIRE_REMOTE: "stroom-test",
  PIPEWIRE_LOG: "0",
};
/**
 * Run a child on the isolated graph and retain its output for assertions.
 * @param {string} command Executable path.
 * @param {string[]} args Command-line arguments.
 * @returns {object} Child with captured log and completion promise.
 */
function run(command, args) {
  const child = spawn(command, args, {
    env: environment,
    stdio: ["ignore", "pipe", "pipe"],
  });
  child.log = "";
  for (const stream of [child.stdout, child.stderr])
    stream.on("data", (data) => {
      child.log += data;
    });
  child.done = once(child, "close");
  children.add(child);
  child.once("close", () => children.delete(child));
  return child;
}

/**
 * Encode one audio transport record.
 * @param {number} type Shared record type.
 * @param {Buffer} payload Encoded record contents.
 * @returns {Buffer} Header and payload.
 */
function record(type, payload = Buffer.alloc(0)) {
  const header = Buffer.alloc(wire.CD_STREAM_HEADER_BYTES);
  header.write(wire.CD_STREAM_MAGIC);
  header.writeUInt32BE(type, 4);
  header.writeUInt32BE(payload.length, 8);
  return Buffer.concat([header, payload]);
}

const format = Buffer.alloc(12);
format.writeUInt32BE(audio.AUDIO_RATE);
format.writeUInt32BE(wire.CD_STREAM_CHANNELS, 4);
format.writeUInt32BE(wire.CD_STREAM_BITS, 8);
const pcm = Buffer.concat(
  Array.from({ length: 12 }, () =>
    record(
      wire.CD_STREAM_PCM,
      Buffer.alloc(wire.CD_STREAM_MAX_PAYLOAD_BYTES, 37),
    ),
  ),
);
const sessions = 20;

/**
 * Wait for a test condition within a bounded interval.
 * @param {Function} condition Synchronous or asynchronous predicate.
 * @param {Function} description Failure diagnostic supplier.
 * @returns {Promise<void>} Resolves once the predicate succeeds.
 */
async function until(condition, description) {
  const deadline = Date.now() + 5000;
  while (!(await condition())) {
    assert(Date.now() < deadline, description());
    await sleep(20);
  }
}

/**
 * Configure a new playback node as a desktop session manager would.
 * @param {object} listener Running native listener child.
 * @param {boolean} capture Whether to link the configured recording node too.
 * @returns {Promise<void>} Resolves after both channels are linked.
 */
async function connectPlayback(listener, capture = false) {
  let node;
  await until(
    async () => {
      assert.equal(listener.exitCode, null, listener.log);
      const dump = run(link.replace(/pw-link$/, "pw-dump"), []);
      assert.equal((await dump.done)[0], 0, dump.log);
      node = JSON.parse(dump.log).find(
        (item) =>
          item.type === "PipeWire:Interface:Node" &&
          item.info?.props?.["node.name"] === "stroom-cd-listener",
      );
      return node;
    },
    () => "No playback node: " + listener.log,
  );
  const configure = run(link.replace(/pw-link$/, "pw-cli"), [
    "set-param",
    String(node.id),
    "PortConfig",
    "{ direction: Output, mode: dsp, format: { mediaType: audio, mediaSubtype: raw, format: F32P, rate: 48000, channels: 2, position: [ FL FR ] } }",
  ]);
  assert.equal((await configure.done)[0], 0, configure.log);
  await until(
    async () => {
      const ports = run(link, ["-o"]);
      assert.equal((await ports.done)[0], 0, ports.log);
      return ports.log.includes("stroom-cd-listener:output_FL");
    },
    () => "No output ports: " + listener.log,
  );
  if (capture) {
    for (const channel of ["FL", "FR"]) {
      const connection = run(link, [
        `stroom-cd-listener:output_${channel}`,
        `stroom-test-capture:input_${channel}`,
      ]);
      assert.equal(
        (await connection.done)[0],
        0,
        connection.log + listener.log,
      );
    }
  }
  for (const channel of ["FL", "FR"]) {
    const connection = run(link, [
      `stroom-cd-listener:output_${channel}`,
      `stroom-test-sink:playback_${channel}`,
    ]);
    assert.equal((await connection.done)[0], 0, connection.log + listener.log);
  }
}

/**
 * Start a raw PCM recorder on the private graph and expose its input ports.
 * @param {string} path Destination for captured interleaved PCM16.
 * @returns {Promise<object>} Recorder child ready to link.
 */
async function recorder(path) {
  const recording = run(captureProgram, [
    "--record",
    "--raw",
    "--rate",
    "48000",
    "--channels",
    "2",
    "--format",
    "s16",
    "--target",
    "0",
    "--properties",
    "{ node.name = stroom-test-capture }",
    path,
  ]);
  let node;
  await until(
    async () => {
      assert.equal(recording.exitCode, null, recording.log);
      const dump = run(link.replace(/pw-link$/, "pw-dump"), []);
      assert.equal((await dump.done)[0], 0, dump.log);
      node = JSON.parse(dump.log).find(
        (item) =>
          item.type === "PipeWire:Interface:Node" &&
          item.info?.props?.["node.name"] === "stroom-test-capture",
      );
      return node;
    },
    () => "No recording node: " + recording.log,
  );
  const configure = run(link.replace(/pw-link$/, "pw-cli"), [
    "set-param",
    String(node.id),
    "PortConfig",
    "{ direction: Input, mode: dsp, format: { mediaType: audio, mediaSubtype: raw, format: F32P, rate: 48000, channels: 2, position: [ FL FR ] } }",
  ]);
  assert.equal((await configure.done)[0], 0, configure.log);
  await until(
    async () => {
      const ports = run(link, ["-i"]);
      assert.equal((await ports.done)[0], 0, ports.log);
      return ports.log.includes("stroom-test-capture:input_FL");
    },
    () => "No recording ports: " + recording.log,
  );
  return recording;
}

/**
 * Encode nonrepeating stereo frames that span several listener ring wraps.
 * @returns {Buffer} Eight seconds of uniquely numbered PCM16 frames.
 */
function numberedSamples() {
  const samples = Buffer.alloc(48000 * 8 * 4);
  for (let i = 0; i < samples.length / 4; ++i) {
    samples.writeInt16LE((i & 65535) - 32768, i * 4);
    samples.writeInt16LE(1024 + (i >>> 16), i * 4 + 2);
  }
  return samples;
}

/**
 * Sample resources between sessions, after the previous player is destroyed.
 * @param {number} pid Native listener process ID.
 * @returns {Promise<object>} Descriptor/thread counts and resident KiB.
 */
async function resources(pid) {
  const root = `/proc/${pid}`;
  const [fds, threads, status] = await Promise.all([
    readdir(root + "/fd"),
    readdir(root + "/task"),
    readFile(root + "/status", "utf8"),
  ]);
  return {
    fds: fds.length,
    threads: threads.length,
    rss: Number(status.match(/^VmRSS:\s+(\d+)/m)[1]),
  };
}

let service;
try {
  const configuration = join(directory, "pipewire.conf");
  await writeFile(
    configuration,
    `
context.properties = {
  core.daemon = true
  core.name = stroom-test
  default.clock.rate = 48000
  default.clock.quantum = 1024
}
context.spa-libs = {
  audio.convert.* = audioconvert/libspa-audioconvert
  support.* = support/libspa-support
}
context.modules = [
  { name = libpipewire-module-protocol-native }
  { name = libpipewire-module-client-node }
  { name = libpipewire-module-adapter }
  { name = libpipewire-module-link-factory }
  { name = libpipewire-module-access }
]
context.objects = [
  { factory = adapter args = {
    factory.name = support.null-audio-sink
    node.name = stroom-test-sink
    media.class = Audio/Sink
    audio.position = [ FL FR ]
    adapter.auto-port-config = { mode = dsp monitor = false position = preserve }
  } }
]
`,
  );
  const daemon = run(pipewire, ["-c", configuration]);
  for (let i = 0; ; ++i) {
    try {
      await access(join(directory, "stroom-test"));
      break;
    } catch {
      if (i > 100 || daemon.exitCode !== null)
        throw Error("Private PipeWire failed: " + daemon.log);
      await sleep(20);
    }
  }
  const subscriptions = [];
  service = net.createServer((socket) => {
    sockets.add(socket);
    socket.on("close", () => sockets.delete(socket));
    socket.on("error", () => {});
    let request = "";
    socket.on("data", (bytes) => {
      request += bytes.toString();
      if (request === wire.CD_STREAM_REQUEST) {
        subscriptions.push(socket);
        socket.write(record(wire.CD_STREAM_FORMAT, format));
      } else {
        assert(wire.CD_STREAM_REQUEST.startsWith(request), request);
      }
    });
  });
  await new Promise((resolve) =>
    service.listen(wire.CD_STREAM_PORT, "127.0.0.1", resolve),
  );
  // This executable uses the production PipeWire adapter and a scheduling test
  // substitute. It tests real callbacks/draining without requiring RT privileges.
  const listenerConfig = join(directory, "listener.json");
  await writeFile(listenerConfig, JSON.stringify({ host: "127.0.0.1" }));
  const listener = run(binary, ["--config", listenerConfig]);
  const snapshots = [];
  const capturePath = join(directory, "samples.raw");
  let recording;
  let expected;
  for (let session = 0; session <= sessions; ++session) {
    await until(
      () => subscriptions.length === session + 1,
      () => "Session did not reconnect: " + listener.log,
    );
    snapshots.push(await resources(listener.pid));
    const socket = subscriptions[session];
    if (session < sessions)
      socket.end(Buffer.concat([pcm, record(wire.CD_STREAM_END)]));
    else if (captureSamples) {
      recording = await recorder(capturePath);
      expected = numberedSamples();
      const input = Buffer.concat([Buffer.alloc((48000 * 4) / 2), expected]);
      for (
        let offset = 0;
        offset < input.length;
        offset += wire.CD_STREAM_MAX_PAYLOAD_BYTES
      )
        socket.write(
          record(
            wire.CD_STREAM_PCM,
            input.subarray(offset, offset + wire.CD_STREAM_MAX_PAYLOAD_BYTES),
          ),
        );
    } else socket.write(pcm);
    await connectPlayback(listener, session === sessions && captureSamples);
    if (session < sessions) {
      await until(
        () =>
          (
            listener.log.match(
              /queued 0 ms; played 7680 frames; underruns 0/g,
            ) || []
          ).length ===
          session + 1,
        () => "Session failed to drain: " + listener.log,
      );
    }
  }
  // Exclude initialization and allocator warmup from steady-state comparisons.
  const steady = snapshots.slice(5);
  const range = (field) =>
    Math.max(...steady.map((sample) => sample[field])) -
    Math.min(...steady.map((sample) => sample[field]));
  assert(range("fds") <= 1, JSON.stringify(snapshots));
  assert(range("threads") <= 1, JSON.stringify(snapshots));
  assert(range("rss") < 8192, JSON.stringify(snapshots));
  await until(
    () =>
      (listener.log.match(/Audio scheduling:/g) || []).length === sessions + 1,
    () => "Final callback did not run: " + listener.log,
  );
  if (recording) {
    // The graph runs in real time. Wait for the numbered audio and trailing silence.
    const deadline = Date.now() + 15000;
    while ((await stat(capturePath)).size < expected.length + 48000 * 4) {
      assert.equal(listener.exitCode, null, listener.log);
      assert(
        Date.now() < deadline,
        "Recorded audio stopped advancing: " + listener.log + recording.log,
      );
      await sleep(50);
    }
    recording.kill("SIGTERM");
    await recording.done;
    const captured = await readFile(capturePath);
    const start = captured.indexOf(expected.subarray(0, 4096));
    assert(start >= 0, "Numbered audio did not reach PipeWire capture");
    assert.deepEqual(
      captured.subarray(start, start + expected.length),
      expected,
      "PipeWire output repeated, skipped or changed source frames",
    );
    const tail = captured.subarray(start + expected.length);
    assert(tail.length >= 48000, "Capture has no complete starvation interval");
    assert(
      tail.every((byte) => byte === 0),
      "PipeWire replayed old audio after source starvation",
    );
    console.log(
      "PipeWire output preserved all 384000 numbered stereo frames across ring wraps and emitted silence after starvation",
    );
  }
  // Keep TCP open without further PCM. A lost daemon must fail even while idle.
  daemon.kill("SIGTERM");
  await daemon.done;
  await until(
    () => listener.exitCode !== null,
    () => "PipeWire loss was ignored: " + listener.log,
  );
  assert.equal((await listener.done)[0], 1, listener.log);
  assert.match(listener.log, /PipeWire playback failed/);
  console.log(
    `Production PipeWire: ${sessions} drains/reconnects and idle daemon-loss recovery passed; ` +
      `steady-state resource ranges ${JSON.stringify({ fds: range("fds"), threads: range("threads"), rssKiB: range("rss") })} (scheduling substituted)`,
  );
} finally {
  for (const child of children) child.kill("SIGTERM");
  for (const socket of sockets) socket.destroy();
  if (service) await new Promise((resolve) => service.close(resolve));
  await Promise.all([...children].map((child) => child.done));
  await rm(directory, { recursive: true, force: true });
}
