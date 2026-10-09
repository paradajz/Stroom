import net from "node:net";
import crypto from "node:crypto";

import { audio, ariacast, metadata } from "../contracts/load.mjs";

const MAX_QUEUED_MS = 500;

/** Validate an explicitly supplied sender label; unnamed sessions omit it. */
export function validDeviceName(name) {
  return (
    typeof name === "string" &&
    name.trim().length > 0 &&
    !/[\x00-\x1f\x7f]/u.test(name) &&
    Buffer.byteLength(name) <= metadata.METADATA_DEVICE_NAME_MAX_BYTES
  );
}

function maskedFrame(payload, opcode = 2) {
  const header = Buffer.alloc(payload.length < 126 ? 6 : 8);
  header[0] = 0x80 | opcode;
  header[1] = 0x80 | (payload.length < 126 ? payload.length : 126);
  const offset = header.length - 4;
  if (offset === 4) header.writeUInt16BE(payload.length, 2);
  crypto.randomFillSync(header, offset, 4);
  const body = Buffer.from(payload);
  for (let i = 0; i < body.length; ++i) body[i] ^= header[offset + (i % 4)];
  return Buffer.concat([header, body]);
}

/** Send complete PCM packets; readPacket returns null at EOF. */
export function streamPCM(
  address,
  onError,
  {
    port = ariacast.ARIA_STREAM_PORT,
    readPacket,
    onEnd = () => {},
    retryConnection = false,
    listening = false,
    deviceName = "",
  } = {},
) {
  let socket,
    timer,
    retry,
    stopped = false,
    streaming = false;
  let packets = 0;
  function fail(message) {
    if (stopped) return;
    stopped = true;
    clearTimeout(timer);
    clearTimeout(retry);
    socket?.destroy();
    onError(message);
  }
  function connect() {
    if (stopped) return;
    socket = net.createConnection({ host: address, port });
    let buffer = Buffer.alloc(0),
      upgraded = false,
      ready = false,
      awaitingListen = false;
    const key = crypto.randomBytes(16).toString("base64");
    const accept = crypto
      .createHash("sha1")
      .update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
      .digest("base64");
    socket.setTimeout(5000);
    socket.on("connect", () =>
      socket.write(
        `GET /audio HTTP/1.1\r\nHost: ${address}:${port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`,
      ),
    );
    socket.on("timeout", () =>
      socket.destroy(Error("AriaCast handshake timed out")),
    );
    socket.on("error", (error) => {
      if (upgraded || streaming || !retryConnection) fail(error.message);
    });
    socket.on("close", () => {
      clearTimeout(timer);
      if (stopped) return;
      if (upgraded || streaming) fail("AriaCast stream disconnected");
      else if (retryConnection) retry = setTimeout(connect, 1000);
      else fail("AriaCast disconnected before the handshake completed");
    });
    socket.on("data", (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      if (!upgraded) {
        const end = buffer.indexOf("\r\n\r\n");
        if (end < 0) {
          if (buffer.length > 16384)
            fail("AriaCast response headers too large");
          return;
        }
        if (
          !/^HTTP\/1\.1 101(?: |$)/.test(buffer.subarray(0, end).toString())
        ) {
          fail("AriaCast upgrade rejected; check that CD is ejected");
          return;
        }
        const headers = new Map(
          buffer
            .subarray(0, end)
            .toString()
            .split("\r\n")
            .slice(1)
            .map((line) => {
              const colon = line.indexOf(":");
              return [
                line.slice(0, colon).trim().toLowerCase(),
                line.slice(colon + 1).trim(),
              ];
            }),
        );
        if (
          headers.get("sec-websocket-accept") !== accept ||
          headers.get("upgrade")?.toLowerCase() !== "websocket" ||
          !headers
            .get("connection")
            ?.toLowerCase()
            .split(/\s*,\s*/)
            .includes("upgrade")
        ) {
          fail("Invalid AriaCast WebSocket upgrade response");
          return;
        }
        buffer = buffer.subarray(end + 4);
        upgraded = true;
      }
      while (!stopped && buffer.length >= 2) {
        if (buffer[0] & 0x70 || !(buffer[0] & 0x80) || buffer[1] & 0x80) {
          fail("Unsupported AriaCast server frame");
          return;
        }
        let length = buffer[1] & 127,
          header = 2;
        if (length === 126) {
          if (buffer.length < 4) return;
          length = buffer.readUInt16BE(2);
          header = 4;
        } else if (length === 127) {
          fail("Unexpected oversized AriaCast frame");
          return;
        }
        if (buffer.length < header + length) return;
        const opcode = buffer[0] & 15;
        const payload = buffer.subarray(header, header + length);
        buffer = buffer.subarray(header + length);
        if (opcode === 8) {
          fail("AriaCast closed the audio session");
          return;
        }
        if (opcode === 9) socket.write(maskedFrame(payload, 10));
        if (opcode === 1 && !ready) {
          if (awaitingListen) {
            if (payload.toString() !== ariacast.ARIA_LISTEN_ACK) {
              fail("Receiver did not confirm listening mode");
              return;
            }
          } else {
            let message;
            try {
              message = JSON.parse(payload.toString());
            } catch {
              continue;
            }
            if (message.status !== "READY") continue;
            if (
              message.sample_rate !== audio.AUDIO_RATE ||
              message.channels !== ariacast.ARIA_PCM_CHANNELS ||
              message.frame_size !== ariacast.ARIA_PCM_BYTES
            ) {
              fail("Unexpected AriaCast PCM format");
              return;
            }
            if (listening) {
              if (message.stroom_listen !== true) {
                fail(
                  "Receiver does not support listening mode; update the PS2 app",
                );
                return;
              }
              if (
                deviceName &&
                (!Number.isInteger(message.stroom_device_name_bytes) ||
                  Buffer.byteLength(deviceName) >
                    message.stroom_device_name_bytes)
              ) {
                fail(
                  "Receiver does not support this device name; update the PS2 app",
                );
                return;
              }
              awaitingListen = true;
              const request = deviceName
                ? `${ariacast.ARIA_LISTEN_REQUEST} ${deviceName}`
                : ariacast.ARIA_LISTEN_REQUEST;
              socket.write(maskedFrame(Buffer.from(request), 1));
              continue;
            }
          }
          ready = streaming = true;
          socket.setTimeout(0);
          let due = performance.now() + ariacast.ARIA_PCM_MESSAGE_MS;
          async function send() {
            if (stopped) return;
            if (performance.now() - due > 500) {
              fail("Host PCM pacing stalled for more than 500ms");
              return;
            }
            if (
              socket.writableLength >
              ariacast.ARIA_PCM_BYTES *
                (MAX_QUEUED_MS / ariacast.ARIA_PCM_MESSAGE_MS)
            ) {
              fail("AriaCast transport cannot keep pace");
              return;
            }
            let packet;
            const readStarted = performance.now();
            try {
              packet = await readPacket();
            } catch (error) {
              fail(error.message);
              return;
            }
            if (stopped) return;
            if (packet === null) {
              stopped = true;
              socket.end();
              onEnd();
              return;
            }
            if (
              !Buffer.isBuffer(packet) ||
              packet.length !== ariacast.ARIA_PCM_BYTES
            ) {
              fail("PCM source returned an invalid packet");
              return;
            }
            socket.write(maskedFrame(packet));
            // Live capture supplies its own clock; do not catch up after an input gap.
            if (performance.now() - readStarted >= ariacast.ARIA_PCM_MESSAGE_MS)
              due = performance.now();
            ++packets;
            due += ariacast.ARIA_PCM_MESSAGE_MS;
            timer = setTimeout(send, Math.max(0, due - performance.now()));
          }
          timer = setTimeout(send, ariacast.ARIA_PCM_MESSAGE_MS);
        }
      }
    });
  }
  connect();
  return {
    get packets() {
      return packets;
    },
    close() {
      stopped = true;
      clearTimeout(timer);
      clearTimeout(retry);
      socket?.destroy();
    },
  };
}
