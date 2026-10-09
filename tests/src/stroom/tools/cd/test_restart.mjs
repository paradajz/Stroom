import assert from "node:assert/strict";
import net from "node:net";
import dgram from "node:dgram";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { mkdtemp, readFile, writeFile, rm } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import {
  audio,
  cdStream as wire,
} from "../../../../../tools/contracts/load.mjs";

const [binary] = process.argv.slice(2);
const directory = await mkdtemp(join(tmpdir(), "stroom-listener-restart-"));
const sockets = new Set();
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(condition, message, timeout = 3000) {
  const deadline = Date.now() + timeout;
  while (!(await condition())) {
    assert(Date.now() < deadline, message);
    await sleep(10);
  }
}
function record(type, payload = Buffer.alloc(0)) {
  const header = Buffer.alloc(wire.CD_STREAM_HEADER_BYTES);
  header.write(wire.CD_STREAM_MAGIC);
  header.writeUInt32BE(type, 4);
  header.writeUInt32BE(payload.length, 8);
  return Buffer.concat([header, payload]);
}
const format = Buffer.alloc(wire.CD_STREAM_FORMAT_BYTES);
format.writeUInt32BE(audio.AUDIO_RATE);
format.writeUInt32BE(wire.CD_STREAM_CHANNELS, 4);
format.writeUInt32BE(wire.CD_STREAM_BITS, 8);
const opening = Buffer.concat(
  Array.from({ length: 12 }, (_, i) =>
    Buffer.alloc(wire.CD_STREAM_MAX_PAYLOAD_BYTES, i + 1),
  ),
);
let connections = 0,
  probes = 0,
  mode = "paused",
  replacedAt = 0;
let oldPort, oldSocket, listener;
const udp = dgram.createSocket("udp4");
udp.on("message", (query, peer) => {
  const match = /^STROOM_CD_CHECK ([1-9][0-9]{0,4})$/.exec(query.toString());
  assert(match, query.toString());
  const port = Number(match[1]);
  assert.equal(port, oldPort);
  ++probes;
  if (mode === "missing") return;
  const reply = {
    service: wire.CD_STREAM_SERVICE,
    port,
    subscribed: mode !== "replaced",
  };
  if (mode === "invalid") {
    udp.send("not json", peer.port, peer.address);
    udp.send(
      JSON.stringify({ ...reply, port: port + 1, subscribed: false }),
      peer.port,
      peer.address,
    );
    udp.send(
      JSON.stringify({ ...reply, service: "other", subscribed: false }),
      peer.port,
      peer.address,
    );
    return;
  }
  udp.send(JSON.stringify(reply), peer.port, peer.address);
});
const server = net.createServer((socket) => {
  sockets.add(socket);
  socket.on("error", () => {});
  socket.once("close", () => sockets.delete(socket));
  let request = "";
  socket.on("data", function subscribe(bytes) {
    request += bytes;
    if (!request.endsWith("\n")) return;
    assert.equal(request, wire.CD_STREAM_REQUEST);
    socket.off("data", subscribe);
    if (++connections === 1) {
      oldPort = socket.remotePort;
      oldSocket = socket;
      // The old connection stays open, as when a reboot disappears without FIN.
      // Keep less than the reserve queued, to verify it is discarded on reconnect.
      socket.write(
        Buffer.concat([
          record(wire.CD_STREAM_FORMAT, format),
          record(
            wire.CD_STREAM_PCM,
            Buffer.alloc(wire.CD_STREAM_MAX_PAYLOAD_BYTES, 255),
          ),
        ]),
      );
    } else if (connections === 2) {
      assert(
        Date.now() - replacedAt < 1500,
        "Reconnect must fit inside the console's two-second opening window",
      );
      const records = [record(wire.CD_STREAM_FORMAT, format)];
      for (let i = 0; i < opening.length; i += wire.CD_STREAM_MAX_PAYLOAD_BYTES)
        records.push(
          record(
            wire.CD_STREAM_PCM,
            opening.subarray(i, i + wire.CD_STREAM_MAX_PAYLOAD_BYTES),
          ),
        );
      socket.end(Buffer.concat([...records, record(wire.CD_STREAM_END)]));
    } else socket.write(record(wire.CD_STREAM_FORMAT, format));
  });
});
try {
  await new Promise((resolve) =>
    server.listen(wire.CD_STREAM_PORT, "127.0.0.1", resolve),
  );
  await new Promise((resolve) =>
    udp.bind(wire.CD_STREAM_PORT, "127.0.0.1", resolve),
  );
  const recording = join(directory, "audio.pcm");
  const configuration = join(directory, "listener.json");
  await writeFile(
    configuration,
    JSON.stringify({ host: "127.0.0.1", buffer_ms: 500 }),
  );
  listener = spawn(binary, ["--config", configuration], {
    env: { ...process.env, STROOM_TEST_PCM: recording },
    stdio: ["ignore", "pipe", "pipe"],
  });
  let log = "";
  listener.stdout.on("data", (data) => {
    log += data;
  });
  listener.stderr.on("data", (data) => {
    log += data;
  });
  const done = once(listener, "close");
  await until(() => probes >= 3, "Paused subscription was never checked");
  assert.equal(connections, 1, log);
  mode = "invalid";
  const invalidAt = probes;
  await until(
    () => probes >= invalidAt + 2,
    "Invalid replies were not exercised",
  );
  assert.equal(
    connections,
    1,
    "Malformed, wrong-port and wrong-service replies must not cancel a pause",
  );
  mode = "missing";
  // Older consoles ignore the new query; retain a healthy idle TCP connection
  // beyond the ordinary handshake deadline instead of treating silence as failure.
  await sleep(5500);
  assert.equal(
    connections,
    1,
    "Missing UDP replies must not expire paused/legacy sessions",
  );
  assert.equal(
    (await readFile(recording)).length,
    0,
    "Old incomplete reserve should still be buffered",
  );
  replacedAt = Date.now();
  mode = "replaced";
  await until(
    () => log.includes("PC playback: queued 0"),
    "Replacement session did not drain its opening",
    2000,
  );
  assert(log.includes("CD subscription lost; reconnecting..."), log);
  await until(() => oldSocket.destroyed, "Old TCP connection was not released");
  listener.kill("SIGTERM");
  assert.equal((await done)[0], 143, log);
  listener = undefined;
  assert.deepEqual(
    await readFile(recording),
    opening,
    "Recover the entire new opening without old buffered PCM",
  );
  console.log(
    "PASS: stale subscription reconnects within the opening window; pauses, legacy replies and PCM ordering are preserved",
  );
} finally {
  listener?.kill("SIGKILL");
  for (const socket of sockets) socket.destroy();
  await new Promise((resolve) => server.close(resolve));
  udp.close();
  await rm(directory, { recursive: true, force: true });
}
