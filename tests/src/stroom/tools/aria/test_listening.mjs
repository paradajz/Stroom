import { ariacast } from "../../../../../tools/contracts/load.mjs";
import assert from "node:assert/strict";
import net from "node:net";
import crypto from "node:crypto";
import { streamPCM } from "../../../../../tools/aria/ariacast.mjs";

for (const mode of [
  "accepted",
  "named",
  "unsupported",
  "wrong-ack",
  "name-unsupported",
]) {
  const success = mode === "accepted" || mode === "named";
  const deviceName =
    mode === "named" || mode === "name-unsupported"
      ? 'Mac "Studio" / Focusrite 18i20'
      : "";
  let peer,
    stream,
    timeout,
    ackTimer,
    requested = false,
    acknowledged = false,
    reads = 0,
    received = 0;
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
              stroom_listen: mode !== "unsupported",
              ...(mode === "named" ? { stroom_device_name_bytes: 63 } : {}),
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
        while (buffer.length >= 6) {
          assert.ok(buffer[1] & 128);
          const long = (buffer[1] & 127) === 126;
          const size = long ? buffer.readUInt16BE(2) : buffer[1] & 127;
          const header = long ? 8 : 6;
          if (buffer.length < header + size) return;
          const opcode = buffer[0] & 15;
          const payload = Buffer.from(buffer.subarray(header, header + size));
          for (let i = 0; i < size; ++i)
            payload[i] ^= buffer[header - 4 + (i % 4)];
          buffer = buffer.subarray(header + size);
          if (opcode === 1) {
            assert.equal(
              payload.toString(),
              deviceName ? `STROOM LISTEN ${deviceName}` : "STROOM LISTEN",
            );
            assert.equal(requested, false);
            requested = true;
            ackTimer = setTimeout(() => {
              try {
                assert.equal(
                  reads,
                  0,
                  "No capture reads before listening is confirmed",
                );
                assert.equal(received, 0);
                const ack = Buffer.from(success ? "STROOM LISTENING" : "NO");
                acknowledged = true;
                socket.write(
                  Buffer.concat([Buffer.from([0x81, ack.length]), ack]),
                );
              } catch (error) {
                reject(error);
              }
            }, 60);
          } else {
            assert.equal(opcode, 2);
            assert.ok(success);
            assert.ok(acknowledged);
            assert.equal(size, ariacast.ARIA_PCM_BYTES);
            ++received;
          }
        }
      } catch (error) {
        reject(error);
      }
    });
    socket.on("end", () => {
      if (success) {
        try {
          assert.equal(received, 1);
          resolve();
        } catch (error) {
          reject(error);
        }
      }
    });
  });
  await new Promise((r) => server.listen(0, "127.0.0.1", r));
  try {
    timeout = setTimeout(
      () => reject(Error("Listening negotiation timed out")),
      2000,
    );
    stream = streamPCM(
      "127.0.0.1",
      (message) => {
        try {
          assert.equal(success, false, message);
          assert.equal(reads, 0);
          assert.equal(received, 0);
          assert.match(
            message,
            mode.includes("unsupported")
              ? /does not support/
              : /did not confirm/,
          );
          resolve();
        } catch (error) {
          reject(error);
        }
      },
      {
        port: server.address().port,
        listening: true,
        deviceName,
        readPacket() {
          return reads++ === 0 ? Buffer.alloc(ariacast.ARIA_PCM_BYTES) : null;
        },
      },
    );
    await complete;
  } finally {
    clearTimeout(timeout);
    clearTimeout(ackTimer);
    stream?.close();
    peer?.destroy();
    await new Promise((r) => server.close(r));
  }
}
console.log(
  "PASS: listening requires advertised support and confirmation before any PCM; ordinary errors cannot enable sound",
);
