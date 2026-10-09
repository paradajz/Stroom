import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import net from "node:net";
import dgram from "node:dgram";
import { once } from "node:events";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
  audio,
  cdStream as wire,
} from "../../../../../tools/contracts/load.mjs";

const [binary, fixturePath, production] = process.argv.slice(2);
const directory = await mkdtemp(join(tmpdir(), "stroom-native-listener-"));
const children = new Set();
const sockets = new Set();
let sequence = 0;
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(predicate, description) {
  const deadline = Date.now() + 6000;
  while (!(await predicate())) {
    if (Date.now() >= deadline) throw Error("Timed out: " + description);
    await sleep(10);
  }
}
function child(executable, args, env = {}) {
  const process = spawn(executable, args, {
    env: {
      ...globalThis.process.env,
      XDG_CONFIG_HOME: directory,
      ...env,
    },
    stdio: ["pipe", "pipe", "pipe"],
  });
  process.log = "";
  for (const stream of [process.stdout, process.stderr])
    stream.on("data", (data) => {
      process.log += data;
    });
  process.done = once(process, "close");
  children.add(process);
  process.once("close", () => children.delete(process));
  return process;
}
function launch({ list = false, ...settings }, env = {}, executable = binary) {
  const recording = join(directory, `audio-${sequence++}.pcm`);
  const configuration = recording + ".json";
  writeFileSync(configuration, JSON.stringify(settings));
  const args = ["--config", configuration];
  if (list) args.push("--list");
  const process = child(executable, args, {
    STROOM_TEST_PCM: recording,
    ...env,
  });
  process.recording = recording;
  return process;
}
async function recorded(process) {
  try {
    return await readFile(process.recording);
  } catch (error) {
    if (error.code === "ENOENT") return Buffer.alloc(0);
    throw error;
  }
}
function record(type, data = Buffer.alloc(0)) {
  const header = Buffer.alloc(wire.CD_STREAM_HEADER_BYTES);
  header.write(wire.CD_STREAM_MAGIC);
  header.writeUInt32BE(type, 4);
  header.writeUInt32BE(data.length, 8);
  return Buffer.concat([header, data]);
}
const format = Buffer.alloc(12);
format.writeUInt32BE(audio.AUDIO_RATE);
format.writeUInt32BE(wire.CD_STREAM_CHANNELS, 4);
format.writeUInt32BE(wire.CD_STREAM_BITS, 8);
const first = record(wire.CD_STREAM_FORMAT, format);
const pcm = Buffer.from(
  Array.from(
    { length: wire.CD_STREAM_MAX_PAYLOAD_BYTES },
    (_, i) => (i * 37) & 255,
  ),
);
const payload = record(wire.CD_STREAM_PCM, pcm);
const end = record(wire.CD_STREAM_END);
async function server(handler) {
  const service = net.createServer((socket) => {
    sockets.add(socket);
    socket.once("close", () => sockets.delete(socket));
    socket.on("error", () => {});
    let request = "";
    socket.on("data", function subscribe(data) {
      request += data;
      if (!request.endsWith("\n")) return;
      assert.equal(request, wire.CD_STREAM_REQUEST);
      socket.off("data", subscribe);
      handler(socket);
    });
  });
  await new Promise((resolve) =>
    service.listen(wire.CD_STREAM_PORT, "127.0.0.1", resolve),
  );
  return service;
}
async function close(service) {
  for (const socket of sockets) socket.destroy();
  await new Promise((resolve) => service.close(resolve));
}
async function stop(process) {
  process.kill("SIGTERM");
  const [code] = await process.done;
  assert.equal(code, 143, process.log);
}
try {
  const help = spawnSync(production, ["--help"], { encoding: "utf8" });
  assert.equal(help.status, 0, help.stderr);
  assert.match(help.stdout, /preferences in listener\.json/);
  for (const args of [
    ["--port", "0"],
    ["--port", "65536"],
    ["--host", ""],
    ["--host", "example.com"],
    ["--buffer-ms", "-1"],
    ["--buffer-ms=2001"],
    ["--buffer-ms=1.5"],
    ["--once"],
    ["--once=yes"],
  ])
    assert.equal(spawnSync(binary, args).status, 1, args.join(" "));

  // Exercise the real PS2 transport fixture, including its retained opening.
  if (fixturePath) {
    const fixture = child(fixturePath, []);
    await until(() => fixture.log.includes("READY"), "PS2 fixture startup");
    fixture.stdin.write("P".repeat(12) + "B");
    await until(() => fixture.log.includes("BARRIER"), "retained opening");
    const opening = launch({
      host: "127.0.0.1",
      buffer_ms: 50,
    });
    await until(() => fixture.log.includes("CLIENTS 1"), "native subscription");
    fixture.stdin.write("PPE");
    await until(
      () => opening.log.includes("PC playback: queued 0"),
      "opening drain",
    );
    await stop(opening);
    assert.deepEqual(
      await recorded(opening),
      Buffer.concat(Array(14).fill(pcm)),
    );
    assert.match(opening.log, /PC playback: queued 0 ms; underruns 0/);
    fixture.stdin.end("Q");
    assert.equal((await fixture.done)[0], 0, fixture.log);
  }

  // An explicit END drains a clip shorter than the reserve, byte for byte.
  let completed = false;
  let service = await server((socket) => {
    if (completed) {
      socket.write(first);
      return;
    }
    completed = true;
    const bytes = Buffer.concat([first, payload, end]);
    for (let i = 0; i < bytes.length; i += 7)
      socket.write(bytes.subarray(i, i + 7));
    socket.end();
  });
  let listener = launch({
    host: "127.0.0.1",
  });
  await until(
    () => listener.log.includes("PC playback: queued 0"),
    "short drain",
  );
  await stop(listener);
  assert.deepEqual(await recorded(listener), pcm);
  await close(service);

  // Pause/reset cancels the old reserve; the next session gets fresh audio.
  let connections = 0;
  service = await server((socket) => {
    if (++connections === 1) {
      socket.write(Buffer.concat([first, payload]));
      setTimeout(() => socket.destroy(), 100);
    } else socket.end(Buffer.concat([first, payload, payload, end]));
  });
  listener = launch({ host: "127.0.0.1" });
  await until(
    async () => (await recorded(listener)).length >= pcm.length * 2,
    "fresh session after reset",
  );
  await stop(listener);
  assert.deepEqual(await recorded(listener), Buffer.concat([pcm, pcm]));
  await close(service);

  // A scheduling denial terminates playback with an explicit error.
  service = await server((socket) =>
    socket.write(Buffer.concat([first, payload])),
  );
  listener = launch({ host: "127.0.0.1" }, { STROOM_TEST_AUDIO_ERROR: "1" });
  assert.equal((await listener.done)[0], 1, listener.log);
  assert.match(listener.log, /real-time scheduling denial/);
  await close(service);

  // Production scheduling setup reports a missing system bus explicitly.
  service = await server((socket) =>
    socket.write(Buffer.concat([first, payload])),
  );
  listener = launch(
    { host: "127.0.0.1" },
    { DBUS_SYSTEM_BUS_ADDRESS: "unix:path=/nonexistent-stroom-test-bus" },
    production,
  );
  assert.equal((await listener.done)[0], 1, listener.log);
  assert.match(listener.log, /RealtimeKit/);
  await close(service);

  // Reject malformed and missing terminal records; preserve shutdown while idle.
  for (const [bytes, error] of [
    [payload, "Unexpected CD stream record"],
    [first, "CD playback interrupted"],
    [Buffer.concat([first, Buffer.from("BAD!")]), "Truncated CD stream"],
    [Buffer.concat([first, end, Buffer.from([0])]), "Data after CD stream end"],
  ]) {
    let attempts = 0;
    service = await server((socket) => {
      if (++attempts === 1) socket.end(bytes);
      else if (attempts === 2) socket.end(Buffer.concat([first, payload, end]));
      else socket.write(first);
    });
    listener = launch({
      host: "127.0.0.1",
    });
    await until(() => listener.log.includes(error), error);
    await until(
      () => listener.log.includes("PC playback: queued 0"),
      "recovery after invalid stream",
    );
    await stop(listener);
    assert.deepEqual(await recorded(listener), pcm);
    await close(service);
  }
  service = await server((socket) => socket.write(first));
  listener = launch({ host: "127.0.0.1" });
  await until(
    () => listener.log.includes("waiting for CD audio"),
    "idle format",
  );
  const started = Date.now();
  await stop(listener);
  assert(Date.now() - started < 500, listener.log);
  assert.equal((await recorded(listener)).length, 0);
  await close(service);

  // The list command validates wire identity and format and never opens audio.
  const peer = dgram.createSocket("udp4");
  peer.on("message", (data, remote) => {
    assert.equal(data.toString(), wire.CD_STREAM_DISCOVER);
    const response = {
      service: wire.CD_STREAM_SERVICE,
      version: wire.CD_STREAM_VERSION,
      sample_rate: audio.AUDIO_RATE,
      channels: wire.CD_STREAM_CHANNELS,
      bits: wire.CD_STREAM_BITS,
      port: peer.address().port,
    };
    peer.send(
      JSON.stringify({ ...response, version: 99 }),
      remote.port,
      remote.address,
    );
    peer.send(JSON.stringify(response), remote.port, remote.address);
  });
  await new Promise((resolve) =>
    peer.bind(wire.CD_STREAM_PORT, "127.0.0.1", resolve),
  );
  try {
    listener = launch(
      { list: true, host: "127.0.0.1" },
      { DBUS_SYSTEM_BUS_ADDRESS: "unix:path=/nonexistent-stroom-test-bus" },
      production,
    );
    assert.equal((await listener.done)[0], 0, listener.log);
    assert.match(
      listener.log,
      new RegExp(`Stroom CD\\t127.0.0.1:${peer.address().port}`),
    );
  } finally {
    peer.close();
  }
  console.log(
    "Native CLI, actual PS2 transport, short drains, reset/reconnect, scheduling failures and idle cancellation passed",
  );
} finally {
  for (const process of children) process.kill("SIGKILL");
  for (const socket of sockets) socket.destroy();
  await rm(directory, { recursive: true, force: true });
}
