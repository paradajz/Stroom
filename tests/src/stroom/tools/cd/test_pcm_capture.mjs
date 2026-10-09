import assert from "node:assert/strict";
import net from "node:net";
import {
  capturePcm,
  repetition,
  wav,
} from "../../../../../tools/cd/listener/diagnose-pcm.mjs";
import {
  audio,
  cdStream as wire,
} from "../../../../../tools/contracts/load.mjs";

/**
 * Encode one independently constructed audio record for the capture fixture.
 * @param {number} type Record type.
 * @param {Buffer} payload Record body.
 * @returns {Buffer} Framed record.
 */
function record(type, payload = Buffer.alloc(0)) {
  const header = Buffer.alloc(12);
  header.write("SCD1");
  header.writeUInt32BE(type, 4);
  header.writeUInt32BE(payload.length, 8);
  return Buffer.concat([header, payload]);
}

/**
 * Run the real capture against a fragmented loopback subscription.
 * @param {Buffer} records Bytes sent after the subscription.
 * @param {object} options Duration override and whether to leave TCP open.
 * @returns {Promise<Buffer>} Captured audio, or a validation failure.
 */
async function capture(records, { seconds = 1, open = false } = {}) {
  const sockets = new Set();
  const server = net.createServer((socket) => {
    sockets.add(socket);
    socket.on("error", () => {});
    socket.on("close", () => sockets.delete(socket));
    socket.once("data", (request) => {
      assert.equal(request.toString(), "STROOM CD 1\n");
      socket.write(records.subarray(0, 3));
      setImmediate(() => {
        if (open) socket.write(records.subarray(3));
        else socket.end(records.subarray(3));
      });
    });
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  try {
    return await capturePcm({
      host: "127.0.0.1",
      port: server.address().port,
      seconds,
    });
  } finally {
    for (const socket of sockets) socket.destroy();
    await new Promise((resolve) => server.close(resolve));
  }
}

const format = Buffer.alloc(12);
format.writeUInt32BE(audio.AUDIO_RATE);
format.writeUInt32BE(2, 4);
format.writeUInt32BE(16, 8);
const initial = record(wire.CD_STREAM_FORMAT, format);
const loop = Buffer.alloc(4800 * 4);
for (let i = 0; i < 4800; ++i) {
  loop.writeInt16LE(i - 2400, i * 4);
  loop.writeInt16LE(2400 - i, i * 4 + 2);
}
const pcm = Buffer.concat(Array.from({ length: 30 }, () => loop));
const records = [initial];
for (let offset = 0; offset < pcm.length; offset += 2560)
  records.push(record(wire.CD_STREAM_PCM, pcm.subarray(offset, offset + 2560)));
records.push(record(wire.CD_STREAM_END));
assert.deepEqual(await capture(Buffer.concat(records)), pcm);
assert.match(repetition(pcm), /100\.000 ms across 3\.00 seconds/);
assert.match(repetition(Buffer.alloc(48000 * 4 * 3)), /digital silence/);
const unique = Buffer.alloc(48000 * 4 * 3);
for (let i = 0; i < unique.length / 4; ++i) unique.writeUInt32LE(i + 1, i * 4);
assert.match(repetition(unique), /No exact loop/);
const file = wav(pcm);
assert.equal(file.toString("ascii", 0, 4), "RIFF");
assert.equal(file.readUInt32LE(4), file.length - 8);
assert.equal(file.readUInt32LE(24), 48000);
assert.equal(file.readUInt32LE(40), pcm.length);
assert.deepEqual(file.subarray(44), pcm);
await assert.rejects(
  capture(record(wire.CD_STREAM_PCM, loop.subarray(0, 2560))),
  /Unexpected/,
);
const oversized = Buffer.from(initial);
oversized.writeUInt32BE(2561, 8);
await assert.rejects(capture(oversized), /header or size/);
const invalidMagic = Buffer.from(initial);
invalidMagic[0] |= 128;
await assert.rejects(capture(invalidMagic), /header or size/);
await assert.rejects(
  capture(
    Buffer.concat([initial, record(wire.CD_STREAM_PCM, Buffer.alloc(3))]),
  ),
  /Unexpected/,
);
await assert.rejects(
  capture(
    Buffer.concat([
      initial,
      record(wire.CD_STREAM_PCM, loop.subarray(0, 2560)),
    ]),
  ),
  /without a clean END/,
);
assert.deepEqual(
  await capture(
    Buffer.concat([
      initial,
      record(wire.CD_STREAM_PCM, loop.subarray(0, 2560)),
    ]),
    { seconds: 0.05, open: true },
  ),
  loop.subarray(0, 2560),
);
console.log(
  "Direct PCM capture, fragmented framing, bounded deadline, WAV output and exact-cycle diagnosis passed",
);
