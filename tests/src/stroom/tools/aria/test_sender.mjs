import { ariacast } from "../../../../../tools/contracts/load.mjs";
import assert from "node:assert/strict";
import { Readable } from "node:stream";
import net from "node:net";
import crypto from "node:crypto";
import { spawn, spawnSync } from "node:child_process";
import { stereoPackets } from "../../../../../tools/aria/pcm_input.mjs";

const cli = new URL("../../../../../tools/aria/sender.mjs", import.meta.url);

// Distinct signed samples in every channel catch off-by-one and byte-order errors.
const frames = ariacast.ARIA_PCM_FRAMES + 7;
const raw = Buffer.alloc(frames * 10 * 2);
const expected = Buffer.alloc(ariacast.ARIA_PCM_BYTES * 2);
for (let i = 0; i < frames; ++i) {
  for (let c = 0; c < 10; ++c)
    raw.writeInt16LE(i * 10 + c - 16000, (i * 10 + c) * 2);
  expected.writeInt16LE(i * 10 + 8 - 16000, i * 4);
  expected.writeInt16LE(i * 10 + 9 - 16000, i * 4 + 2);
}
const chunks = [];
for (let i = 0; i < raw.length; i += 137) chunks.push(raw.subarray(i, i + 137));
async function sendPCM(args, chunks, expected) {
  let peer, child, timer;
  let received = Buffer.alloc(0);
  let resolve, reject;
  const complete = new Promise((yes, no) => {
    resolve = yes;
    reject = no;
  });
  const server = net.createServer((socket) => {
    peer = socket;
    let buffer = Buffer.alloc(0),
      upgraded = false;
    socket.on("error", reject);
    socket.on("data", (chunk) => {
      try {
        buffer = Buffer.concat([buffer, chunk]);
        if (!upgraded) {
          const end = buffer.indexOf("\r\n\r\n");
          if (end < 0) return;
          const key = buffer.toString().match(/Sec-WebSocket-Key: ([^\r]+)/)[1];
          const accept = crypto
            .createHash("sha1")
            .update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
            .digest("base64");
          const ready = Buffer.from(
            JSON.stringify({
              status: "READY",
              sample_rate: 48000,
              channels: 2,
              frame_size: 3840,
            }),
          );
          socket.write(
            Buffer.concat([
              Buffer.from(
                `HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`,
              ),
              Buffer.from([0x81, ready.length]),
              ready,
            ]),
          );
          buffer = buffer.subarray(end + 4);
          upgraded = true;
        }
        while (buffer.length >= 8 + ariacast.ARIA_PCM_BYTES) {
          assert.equal(buffer[0], 0x82);
          assert.equal(buffer[1], 0xfe);
          assert.equal(buffer.readUInt16BE(2), ariacast.ARIA_PCM_BYTES);
          const data = Buffer.from(
            buffer.subarray(8, 8 + ariacast.ARIA_PCM_BYTES),
          );
          for (let i = 0; i < data.length; ++i) data[i] ^= buffer[4 + (i % 4)];
          received = Buffer.concat([received, data]);
          buffer = buffer.subarray(8 + ariacast.ARIA_PCM_BYTES);
        }
      } catch (error) {
        reject(error);
      }
    });
    socket.on("end", () => {
      try {
        assert.deepEqual(received, expected);
        assert.equal(buffer.length, 0);
        resolve();
      } catch (error) {
        reject(error);
      }
    });
  });
  await new Promise((r) =>
    server.listen(ariacast.ARIA_STREAM_PORT, "127.0.0.1", r),
  );
  try {
    timer = setTimeout(() => reject(Error("Sender did not finish")), 3000);
    child = spawn(
      process.execPath,
      [cli.pathname, "--host", "127.0.0.1", ...args],
      { stdio: ["pipe", "ignore", "pipe"] },
    );
    let stderr = "";
    child.stderr.on("data", (data) => {
      stderr += data;
    });
    child.stdin.on("error", reject);
    const exited = new Promise((resolveExit, rejectExit) => {
      child.once("error", rejectExit);
      child.once("close", (code, signal) => {
        try {
          assert.equal(signal, null, stderr);
          assert.equal(code, 0, stderr);
          resolveExit();
        } catch (error) {
          reject(error);
          rejectExit(error);
        }
      });
    });
    for (const chunk of chunks) child.stdin.write(chunk);
    child.stdin.end();
    await Promise.all([complete, exited]);
  } finally {
    clearTimeout(timer);
    child?.kill("SIGKILL");
    peer?.destroy();
    await new Promise((r) => server.close(r));
  }
}

await sendPCM(
  ["--right", "10", "--channels", "10", "--left", "9"],
  chunks,
  expected,
);

// Defaults and individual overrides must affect the actual transmitted samples.
const stereo = Buffer.alloc(12);
for (const [i, sample] of [1, -2, 3, -4, 5, -6].entries())
  stereo.writeInt16LE(sample, i * 2);
const normal = Buffer.alloc(ariacast.ARIA_PCM_BYTES);
stereo.copy(normal);
await sendPCM([], [stereo], normal);
const swapped = Buffer.alloc(ariacast.ARIA_PCM_BYTES);
for (let frame = 0; frame < 3; ++frame) {
  swapped.writeInt16LE(stereo.readInt16LE(frame * 4 + 2), frame * 4);
  swapped.writeInt16LE(stereo.readInt16LE(frame * 4), frame * 4 + 2);
}
await sendPCM(["--left=2", "--right=1"], [stereo], swapped);
const mono = Buffer.alloc(6);
const duplicated = Buffer.alloc(ariacast.ARIA_PCM_BYTES);
for (const [i, sample] of [-7, 8, -9].entries()) {
  mono.writeInt16LE(sample, i * 2);
  duplicated.writeInt16LE(sample, i * 4);
  duplicated.writeInt16LE(sample, i * 4 + 2);
}
await sendPCM(["--channels", "1", "--right", "1"], [mono], duplicated);

await assert.rejects(async () => {
  for await (const packet of stereoPackets(
    Readable.from([Buffer.alloc(21)]),
    10,
    9,
    10,
  ))
    void packet;
}, /middle of a PCM/);
await assert.rejects(async () => {
  for await (const packet of stereoPackets(Readable.from([]), 10, 0, 10))
    void packet;
}, /1-based/);
assert.equal(spawnSync(process.execPath, [cli.pathname, "--help"]).status, 0);
for (const args of [
  [],
  ["--left", "1"],
  ["--host"],
  ["--host", ""],
  ["localhost", "10", "9", "10"],
  ["--host", "localhost", "--unknown"],
  ["--host", "localhost", "--stdin"],
  ["--host", "localhost", "--left"],
  ...["0", "65", "1.5", "9x", "-1", "9007199254740992"].map((value) => [
    "--host",
    "localhost",
    "--channels",
    value,
  ]),
  ["--host", "localhost", "--left", "0"],
  ["--host", "localhost", "--right", "3"],
]) {
  assert.equal(spawnSync(process.execPath, [cli.pathname, ...args]).status, 1);
}
console.log(
  "PASS: multichannel stdin conversion, split samples, masked delivery, final padding, EOF and invalid input",
);

for (const name of ["", " ", "bad\nname", "x".repeat(64), "é".repeat(32)]) {
  const result = spawnSync(process.execPath, [
    cli.pathname,
    "--listen",
    "--device-name",
    name,
    "--host",
    "localhost",
  ]);
  assert.equal(result.status, 1);
  assert.match(result.stderr.toString(), /Device names require/);
}
assert.equal(
  spawnSync(process.execPath, [
    cli.pathname,
    "--device-name",
    "Focusrite",
    "--host",
    "localhost",
  ]).status,
  1,
);
