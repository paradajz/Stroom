import assert from "node:assert/strict";
import net from "node:net";
import crypto from "node:crypto";
import { once } from "node:events";
import { spawn } from "node:child_process";
import { ariacast, audio } from "../../../../../tools/contracts/load.mjs";
import { streamPCM } from "../../../../../tools/aria/ariacast.mjs";

async function recovery(kind) {
  const listening = kind !== "audible";
  const peers = new Set();
  const delivered = [],
    errors = [],
    statuses = [];
  let connections = 0,
    reads = 0,
    ended = 0,
    eof = false,
    held,
    stream,
    child,
    captureTimer;
  function packet() {
    const data = Buffer.alloc(ariacast.ARIA_PCM_BYTES, ++reads & 255);
    data.writeUInt32BE(reads);
    return data;
  }
  const server = net.createServer((peer) => {
    peers.add(peer);
    peer.on("close", () => peers.delete(peer));
    peer.on("error", () => {});
    const session = ++connections;
    let buffer = Buffer.alloc(0),
      upgraded = false,
      confirmed = false;
    const text = (value) => {
      const data = Buffer.from(value);
      peer.write(Buffer.concat([Buffer.from([0x81, data.length]), data]));
    };
    peer.on("data", (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      if (!upgraded) {
        const end = buffer.indexOf("\r\n\r\n");
        if (end < 0) return;
        // An old PS2 session can still own the receiver on the first retry.
        if (kind === "prolonged" && session === 2) {
          peer.end("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
          return;
        }
        const key = /Sec-WebSocket-Key: ([^\r]+)/.exec(buffer.toString())[1];
        const accept = crypto
          .createHash("sha1")
          .update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
          .digest("base64");
        peer.write(
          `HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`,
        );
        text(
          JSON.stringify({
            status: "READY",
            sample_rate: audio.AUDIO_RATE,
            channels: ariacast.ARIA_PCM_CHANNELS,
            frame_size: ariacast.ARIA_PCM_BYTES,
            stroom_listen: true,
            stroom_device_name_bytes: 63,
          }),
        );
        buffer = buffer.subarray(end + 4);
        upgraded = true;
      }
      while (buffer.length >= 6) {
        assert(buffer[1] & 128);
        const long = (buffer[1] & 127) === 126;
        const size = long ? buffer.readUInt16BE(2) : buffer[1] & 127;
        const header = long ? 8 : 6;
        if (buffer.length < header + size) return;
        const opcode = buffer[0] & 15;
        const data = Buffer.from(buffer.subarray(header, header + size));
        for (let i = 0; i < size; ++i) data[i] ^= buffer[header - 4 + (i % 4)];
        buffer = buffer.subarray(header + size);
        if (opcode === 1) {
          assert.equal(data.toString(), "STROOM LISTEN Desktop");
          assert.equal(confirmed, false);
          confirmed = true;
          text("STROOM LISTENING");
          if (kind === "cli" && session === 1)
            captureTimer = setInterval(
              () => child.stdin.write(packet()),
              ariacast.ARIA_PCM_MESSAGE_MS,
            );
        } else {
          assert.equal(opcode, 2);
          assert.equal(size, ariacast.ARIA_PCM_BYTES);
          assert(
            !listening || confirmed,
            "Every reconnect must confirm silent mode before PCM",
          );
          const sequence = data.readUInt32BE(0);
          assert.deepEqual(
            data.subarray(4),
            Buffer.alloc(size - 4, sequence & 255),
          );
          delivered.push({ session, sequence });
          if (kind === "cli" && session === 1 && delivered.length === 2)
            peer.destroy();
        }
      }
    });
  });
  await new Promise((resolve) =>
    server.listen(
      kind === "cli" ? ariacast.ARIA_STREAM_PORT : 0,
      "127.0.0.1",
      resolve,
    ),
  );

  // Hold a real socket's first audio write at the transport boundary. This
  // exercises backpressure deterministically without filling OS-sized buffers.
  const createConnection = net.createConnection;
  let created = 0;
  net.createConnection = (...args) => {
    const connection = createConnection(...args);
    const write = connection.write.bind(connection);
    const first = ++created === 1;
    connection.write = (data, ...rest) => {
      if (first && !held && Buffer.isBuffer(data) && data[0] === 0x82) {
        held = {
          connection,
          data,
          callback: rest[0],
          release: () => write(data, ...rest),
        };
        if (!listening)
          Object.defineProperty(connection, "writableLength", {
            get: () => audio.AUDIO_RATE * 4,
          });
        return false;
      }
      return write(data, ...rest);
    };
    return connection;
  };
  async function until(condition, message) {
    const limit = Date.now() + 4000;
    while (!condition()) {
      assert(
        Date.now() < limit,
        `${kind}: ${message}; reads=${reads}; statuses=${statuses}`,
      );
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
  }
  try {
    if (kind === "cli") {
      const cli = new URL(
        "../../../../../tools/aria/sender.mjs",
        import.meta.url,
      );
      child = spawn(
        process.execPath,
        [
          cli.pathname,
          "--host",
          "127.0.0.1",
          "--listen",
          "--device-name",
          "Desktop",
        ],
        { stdio: ["pipe", "ignore", "pipe"] },
      );
      let output = "";
      child.stderr.on("data", (data) => {
        output += data;
      });
      child.stdin.on("error", (error) => errors.push(error.message));
      const exited = once(child, "close");
      await until(
        () =>
          connections === 2 &&
          delivered.filter((p) => p.session === 2).length >= 2,
        "The CLI must reconnect without restarting its stdin capture",
      );
      assert.equal(child.exitCode, null, output);
      assert(reads > 25, "Capture input must continue while disconnected");
      const fresh = delivered.filter((p) => p.session === 2);
      assert(
        reads - fresh[0].sequence <= 4,
        "The CLI must resume near live capture, not buffered stdin history",
      );
      assert.match(output, /Listening reconnected/);
      clearInterval(captureTimer);
      child.stdin.end();
      assert.equal((await exited)[0], 0, output);
      assert.deepEqual(errors, []);
      return;
    }
    stream = streamPCM("127.0.0.1", (error) => errors.push(error), {
      port: server.address().port,
      listening,
      deviceName: listening ? "Desktop" : "",
      onStatus: (message) => statuses.push(message),
      onEnd: () => ++ended,
      readPacket() {
        if (eof) return null;
        return packet();
      },
    });
    await until(() => held, "First audio write was not held");
    if (kind === "audible") {
      await until(
        () => errors.length,
        "Audible playback must fail on an excessive backlog",
      );
      assert.match(errors[0], /transport cannot keep pace/);
      assert.equal(connections, 1);
      return;
    }
    await until(() => reads >= 8, "Capture stopped during backpressure");
    assert.deepEqual(errors, []);
    if (kind === "brief") {
      const skippedUntil = reads;
      held.release();
      await until(
        () => delivered.length >= 3,
        "Brief stall did not resume on the same connection",
      );
      assert.equal(connections, 1);
      assert.equal(delivered[0].sequence, 1);
      assert(
        delivered[1].sequence > skippedUntil,
        "Do not replay samples captured while blocked",
      );
    } else {
      await until(
        () => held.connection.destroyed,
        "Prolonged stall must cancel the old socket",
      );
      const beforeReconnect = reads;
      await until(
        () => connections === 3 && delivered.length >= 2,
        "No automatic reconnection",
      );
      assert(
        reads > beforeReconnect + 10,
        "Capture must continue through reconnection",
      );
      assert(delivered.every((packet) => packet.session === 3));
      assert(delivered[0].sequence > beforeReconnect);
      assert(statuses.some((message) => message.includes("reconnected")));
      // Completion of a cancelled write cannot interfere with the new socket.
      held.callback(Error("old write cancelled"));
    }
    const completed = once([...peers][0], "end");
    eof = true;
    await completed;
    assert.equal(ended, 1);
    assert.deepEqual(errors, []);
    assert.equal(connections, kind === "brief" ? 1 : 3);
  } finally {
    net.createConnection = createConnection;
    clearInterval(captureTimer);
    child?.kill("SIGKILL");
    stream?.close();
    for (const peer of peers) peer.destroy();
    await new Promise((resolve) => server.close(resolve));
  }
}

for (const kind of ["brief", "prolonged", "audible", "cli"])
  await recovery(kind);
console.log(
  "PASS: listening drops stale unsent PCM, recovers, renegotiates silence and keeps capture running; audible backlog remains fatal",
);
