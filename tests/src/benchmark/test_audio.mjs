import assert from "node:assert/strict";
import net from "node:net";
import crypto from "node:crypto";
import {
  generatedPCM,
  streamAudio,
} from "../../../tools/milkdrop/benchmark_audio.mjs";

const pcm = generatedPCM();
assert.equal(pcm.length, 192000);
assert.deepEqual(pcm, generatedPCM());
assert.notDeepEqual(pcm.subarray(0, 3840), pcm.subarray(3840, 7680));
let peer;
const server = net.createServer((socket) => {
  peer = socket;
  let buffer = Buffer.alloc(0),
    upgraded = false;
  socket.on("data", (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    if (!upgraded) {
      const end = buffer.indexOf("\r\n\r\n");
      if (end < 0) return;
      assert.match(
        buffer.subarray(0, end).toString(),
        /^GET \/audio HTTP\/1.1/,
      );
      const request = buffer.subarray(0, end).toString();
      const key = request.match(/Sec-WebSocket-Key: ([^\r]+)\r\n/i)[1];
      const accept = crypto
        .createHash("sha1")
        .update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
        .digest("base64");
      buffer = buffer.subarray(end + 4);
      upgraded = true;
      const ready = Buffer.from(
        JSON.stringify({
          status: "READY",
          sample_rate: 48000,
          channels: 2,
          frame_size: 3840,
        }),
      );
      const reply = Buffer.concat([
        Buffer.from(
          `HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`,
        ),
        Buffer.from([0x81, ready.length]),
        ready,
      ]);
      socket.write(reply.subarray(0, 12));
      setTimeout(() => socket.write(reply.subarray(12)), 10);
    }
    while (buffer.length >= 8) {
      assert.equal(buffer[0], 0x82);
      assert.equal(buffer[1], 0xfe);
      const size = buffer.readUInt16BE(2);
      assert.equal(size, 3840);
      if (buffer.length < 8 + size) return;
      const data = Buffer.from(buffer.subarray(8, 8 + size));
      for (let i = 0; i < size; ++i) data[i] ^= buffer[4 + (i % 4)];
      assert.deepEqual(
        data,
        pcm.subarray(received * 3840, (received + 1) * 3840),
      );
      ++received;
      buffer = buffer.subarray(8 + size);
      if (received === 5) resolve();
    }
  });
});
let received = 0,
  resolve;
const completed = new Promise((r) => (resolve = r));
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const errors = [];
const stream = streamAudio(
  "127.0.0.1",
  (message) => errors.push(message),
  server.address().port,
);
const timeout = setTimeout(() => {
  throw Error("Timed out receiving generated audio");
}, 3000);
try {
  await completed;
  assert.equal(stream.packets, 5);
  assert.deepEqual(errors, []);
} finally {
  clearTimeout(timeout);
  stream.close();
  peer?.destroy();
  await new Promise((r) => server.close(r));
}
console.log(
  "PASS: deterministic stereo PCM, fragmented upgrade/READY, masked frames and paced network delivery",
);

// Startup protocol failures must be reported promptly rather than retried silently.
for (const failure of [
  "status",
  "accept",
  "format",
  "masked",
  "fragmented",
  "headers",
]) {
  let connection;
  const fixture = net.createServer((socket) => {
    connection = socket;
    let request = "",
      sent = false;
    socket.on("data", (chunk) => {
      request += chunk.toString();
      if (sent || !request.includes("\r\n\r\n")) return;
      sent = true;
      if (failure === "headers") {
        socket.write("X".repeat(16385));
        return;
      }
      if (failure === "status") {
        socket.write("HTTP/1.1 403 Forbidden\r\n\r\n");
        return;
      }
      const key = request.match(/Sec-WebSocket-Key: ([^\r]+)\r\n/i)[1];
      const accept =
        failure === "accept"
          ? "invalid"
          : crypto
              .createHash("sha1")
              .update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
              .digest("base64");
      const ready = Buffer.from(
        JSON.stringify({
          status: "READY",
          sample_rate: failure === "format" ? 44100 : 48000,
          channels: 2,
          frame_size: 3840,
        }),
      );
      socket.write(
        Buffer.concat([
          Buffer.from(
            `HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`,
          ),
          Buffer.from([
            failure === "fragmented" ? 0x01 : 0x81,
            ready.length | (failure === "masked" ? 0x80 : 0),
          ]),
          ready,
        ]),
      );
    });
  });
  await new Promise((r) => fixture.listen(0, "127.0.0.1", r));
  let deadline;
  const messages = [];
  let streamUnderTest;
  const failed = new Promise((resolve, reject) => {
    deadline = setTimeout(
      () => reject(Error("Protocol failure was not reported: " + failure)),
      1000,
    );
    streamUnderTest = streamAudio(
      "127.0.0.1",
      (message) => {
        messages.push(message);
        resolve();
      },
      fixture.address().port,
    );
  });
  try {
    await failed;
    await new Promise((r) => setTimeout(r, 25));
    assert.equal(messages.length, 1, "Failure must be reported only once");
    assert.equal(
      streamUnderTest.packets,
      0,
      "Invalid sessions must not send PCM",
    );
  } finally {
    clearTimeout(deadline);
    streamUnderTest?.close();
    connection?.destroy();
    await new Promise((r) => fixture.close(r));
  }
}
console.log(
  "PASS: invalid upgrades, formats, framing and oversized headers fail promptly",
);
