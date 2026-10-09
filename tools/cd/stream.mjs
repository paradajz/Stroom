import dgram from "node:dgram";
import net from "node:net";
import { networkInterfaces } from "node:os";
import { once } from "node:events";
import { audio, cdStream as wire } from "../contracts/load.mjs";

/** Retrieve the saved sender snapshot without opening an audio connection. Older stacks return no reply. */
export function queryCdTransport(source, { timeout = 200, signal } = {}) {
  return new Promise((resolve) => {
    const socket = dgram.createSocket("udp4");
    let timer,
      done = false;
    const finish = (reply = null) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      signal?.removeEventListener("abort", abort);
      try {
        socket.close();
      } catch {
        /* Socket may not have bound. */
      }
      resolve(reply);
    };
    const abort = () => finish();
    socket.on("error", abort);
    socket.on("message", (data, peer) => {
      if (done || peer.port !== source.port || data.length > 1024) return;
      try {
        const reply = JSON.parse(data.toString());
        const counters = ["id", "duration_ms", "sample_ms", "lag", "available"];
        const fields = [
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
        const unsigned = (value) =>
          Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
        const driverFields = [
          "link_mode",
          "tx_dropped",
          "tx_errors",
          "tx_carrier",
          "tx_deferred",
          "tx_collisions",
          "tx_underruns",
          "rx_alloc_fail",
          "rx_overrun",
          "rx_bad_fcs",
        ];
        if (
          reply?.service !== wire.CD_STREAM_SERVICE ||
          reply.traceVersion !== 1 ||
          !counters.every((key) => unsigned(reply[key])) ||
          reply.available > 1 ||
          !reply.tcp ||
          !fields.every((key) => unsigned(reply.tcp[key])) ||
          !Number.isInteger(reply.tcp.timer_ms) ||
          reply.tcp.timer_ms < -1 ||
          reply.tcp.timer_ms > 0x7fffffff ||
          (reply.driver !== undefined &&
            (!reply.driver ||
              ![-1, 0, 1].includes(reply.driver.status) ||
              !driverFields.every((key) => unsigned(reply.driver[key]))))
        )
          return;
        finish(reply);
      } catch {
        /* Ignore malformed or unrelated UDP packets. */
      }
    });
    if (signal?.aborted) {
      finish();
      return;
    }
    signal?.addEventListener("abort", abort, { once: true });
    timer = setTimeout(abort, timeout);
    socket.connect(source.port, source.host, () => {
      if (done) return;
      socket.send(Buffer.from(wire.CD_STREAM_TRACE), (error) => {
        if (error) finish();
      });
    });
  });
}

export function formatCdTransport(trace) {
  if (!trace.available)
    return "PS2 CD TCP diagnostics unavailable: older resident stack or socket query failed.";
  if (!trace.id)
    return "PS2 CD TCP diagnostics available; no send stalls of 250 ms or more recorded.";
  const tcp = trace.tcp;
  const driver = trace.driver;
  const ethernet = !driver
    ? ""
    : driver.status !== 1
      ? `\nPS2 Ethernet transmit counters unavailable (status ${driver.status}; restart console after an app bridge update).`
      : `\nPS2 Ethernet counters at stall (since console boot): link mode ${driver.link_mode} (${["unknown", "10 Mbps half duplex", "10 Mbps full duplex", "100 Mbps half duplex", "100 Mbps full duplex"][driver.link_mode & ~64] ?? "unknown"}; pause ${driver.link_mode & 64 ? "disabled" : "enabled"}); TX dropped ${driver.tx_dropped}; errors ${driver.tx_errors}; carrier loss ${driver.tx_carrier}; excessive deferral ${driver.tx_deferred}; collisions ${driver.tx_collisions}; underruns ${driver.tx_underruns}; RX allocation failures ${driver.rx_alloc_fail}; overruns ${driver.rx_overrun}; bad FCS ${driver.rx_bad_fcs}`;
  return (
    `PS2 CD TCP stall #${trace.id}: duration ${trace.duration_ms} ms; sampled at ${trace.sample_ms} ms; queue ${trace.lag} sectors; send space ${tcp.send_space} bytes; unsent ${tcp.unsent_bytes} bytes/${tcp.unsent_segments} segments (ref ${tcp.unsent_ref}); unacked ${tcp.unacked_bytes} bytes/${tcp.unacked_segments} segments (ref ${tcp.unacked_ref}); ACK ${tcp.last_ack}; next seq ${tcp.next_sequence}; last written ${tcp.last_written}; cwnd ${tcp.congestion_window}; peer window ${tcp.peer_window}; retransmissions ${tcp.retransmissions}; RTO ${tcp.rto_ms} ms; timer ${tcp.timer_ms} ms; persist ${tcp.persist_backoff}; flags 0x${tcp.flags.toString(16)}` +
    ethernet
  );
}

/** Broadcast on each IPv4 subnet so the default route cannot hide another LAN adapter. */
export function discoveryBroadcasts(interfaces = networkInterfaces()) {
  const destinations = new Set();
  const ipv4 = (value) =>
    net.isIPv4(value ?? "")
      ? value
          .split(".")
          .reduce((address, octet) => (address * 256 + Number(octet)) >>> 0, 0)
      : null;
  for (const entries of Object.values(interfaces)) {
    for (const entry of entries ?? []) {
      if (entry.internal || (entry.family !== "IPv4" && entry.family !== 4))
        continue;
      const address = ipv4(entry.address),
        mask = ipv4(entry.netmask);
      if (address === null || mask === null || mask === 0) continue;
      const hostBits = ~mask >>> 0;
      // Point-to-point /31 and /32 interfaces have no subnet broadcast.
      if (hostBits < 3 || (hostBits & (hostBits + 1)) !== 0) continue;
      const broadcast = ((address & mask) | hostBits) >>> 0;
      destinations.add(
        [24, 16, 8, 0].map((shift) => (broadcast >>> shift) & 255).join("."),
      );
    }
  }
  destinations.add("255.255.255.255");
  return [...destinations].sort();
}

/** Discover sources on the LAN; use the reply's source address, never a claimed IP. */
export function discoverCdSources({
  timeout = 1200,
  broadcast,
  port = wire.CD_STREAM_PORT,
  signal,
  includeStats = false,
} = {}) {
  return new Promise((resolve, reject) => {
    const socket = dgram.createSocket("udp4");
    const sources = new Map();
    const destinations =
      broadcast === undefined ? discoveryBroadcasts() : [broadcast];
    let successfulSends = 0,
      sendError;
    let timer,
      interval,
      done = false;
    function finish(error) {
      if (done) return;
      done = true;
      clearTimeout(timer);
      clearInterval(interval);
      signal?.removeEventListener("abort", abort);
      try {
        socket.close();
      } catch {
        /* A bind failure can leave the socket unopened. */
      }
      if (error) reject(error);
      else
        resolve(
          [...sources.values()].sort((a, b) => a.host.localeCompare(b.host)),
        );
    }
    const abort = () => finish(Error("Discovery stopped"));
    socket.on("error", finish);
    socket.on("message", (data, peer) => {
      if (data.length > 1024 || peer.port !== port) return;
      let reply;
      try {
        reply = JSON.parse(data.toString());
      } catch {
        return;
      }
      if (
        !reply ||
        typeof reply !== "object" ||
        Array.isArray(reply) ||
        reply.service !== wire.CD_STREAM_SERVICE ||
        reply.version !== wire.CD_STREAM_VERSION ||
        reply.sample_rate !== audio.AUDIO_RATE ||
        reply.channels !== wire.CD_STREAM_CHANNELS ||
        reply.bits !== wire.CD_STREAM_BITS ||
        !Number.isInteger(reply.port) ||
        reply.port < 1 ||
        reply.port > 65535
      )
        return;
      const source = {
        host: peer.address,
        port: reply.port,
        name: "Stroom CD",
      };
      if (includeStats && reply.stats && typeof reply.stats === "object") {
        const stats = reply.stats;
        const counters = [
          "disconnects",
          "send_retries",
          "resets",
          "mailbox_overflows",
          "max_lag_blocks",
          "max_poll_gap_ms",
        ];
        if (
          counters.every(
            (key) => Number.isSafeInteger(stats[key]) && stats[key] >= 0,
          ) &&
          Number.isSafeInteger(stats.last_error) &&
          typeof stats.last_disconnect === "string" &&
          /^[a-z_]{1,40}$/.test(stats.last_disconnect)
        )
          source.stats = Object.fromEntries(
            [...counters, "last_disconnect", "last_error"].map((key) => [
              key,
              stats[key],
            ]),
          );
      }
      const producer = reply.stats?.source;
      const producerKeys = [
        "blocks",
        "gaps",
        "max_gap_ms",
        "drive_waits",
        "output_busy",
        "silence_blocks",
        "max_drive_wait_ms",
        "max_output_busy_ms",
      ];
      if (
        source.stats &&
        producer &&
        producerKeys.every(
          (key) => Number.isSafeInteger(producer[key]) && producer[key] >= 0,
        )
      )
        source.stats.source = Object.fromEntries(
          producerKeys.map((key) => [key, producer[key]]),
        );
      sources.set(source.host + ":" + source.port, source);
    });
    socket.bind(0, "0.0.0.0", () => {
      if (signal?.aborted) {
        finish(Error("Discovery stopped"));
        return;
      }
      signal?.addEventListener("abort", abort, { once: true });
      try {
        socket.setBroadcast(true);
      } catch (error) {
        finish(error);
        return;
      }
      const query = Buffer.from(wire.CD_STREAM_DISCOVER);
      const send = () => {
        for (const destination of destinations) {
          socket.send(query, port, destination, (error) => {
            if (error) sendError = error;
            else ++successfulSends;
          });
        }
      };
      send();
      interval = setInterval(send, 300);
      timer = setTimeout(
        () => finish(successfulSends ? undefined : sendError),
        timeout,
      );
    });
  });
}

/** Decode bounded records across arbitrary TCP boundaries. END is required for clean EOF. */
export async function* decodeCdStream(chunks) {
  let buffer = Buffer.alloc(0),
    formatted = false;
  for await (const chunk of chunks) {
    buffer = Buffer.concat([buffer, chunk]);
    while (buffer.length >= wire.CD_STREAM_HEADER_BYTES) {
      if (!buffer.subarray(0, 4).equals(Buffer.from(wire.CD_STREAM_MAGIC)))
        throw Error("Invalid CD stream record");
      const type = buffer.readUInt32BE(4),
        size = buffer.readUInt32BE(8);
      if (size > wire.CD_STREAM_MAX_PAYLOAD_BYTES)
        throw Error("CD stream record exceeds its size limit");
      if (buffer.length < wire.CD_STREAM_HEADER_BYTES + size) break;
      const data = buffer.subarray(
        wire.CD_STREAM_HEADER_BYTES,
        wire.CD_STREAM_HEADER_BYTES + size,
      );
      buffer = buffer.subarray(wire.CD_STREAM_HEADER_BYTES + size);
      if (
        type === wire.CD_STREAM_FORMAT &&
        !formatted &&
        size === wire.CD_STREAM_FORMAT_BYTES
      ) {
        if (
          data.readUInt32BE(0) !== audio.AUDIO_RATE ||
          data.readUInt32BE(4) !== wire.CD_STREAM_CHANNELS ||
          data.readUInt32BE(8) !== wire.CD_STREAM_BITS
        )
          throw Error("Unsupported CD audio format");
        formatted = true;
        yield { type: "format" };
      } else if (
        type === wire.CD_STREAM_PCM &&
        formatted &&
        size > 0 &&
        size % 4 === 0
      ) {
        yield { type: "pcm", data };
      } else if (type === wire.CD_STREAM_END && formatted && size === 0) {
        if (buffer.length) throw Error("Data after CD stream end");
        yield { type: "end" };
        return;
      } else {
        throw Error("Unexpected CD stream record");
      }
    }
  }
  throw Error(
    buffer.length ? "Truncated CD stream" : "CD playback interrupted",
  );
}

/** Subscribe to a discovered source. A seek/pause closes the old connection. */
export async function* receiveCdStream(source, { signal } = {}) {
  const socket = net.createConnection({
    host: source.host,
    port: source.port,
    signal,
    keepAlive: true,
    keepAliveInitialDelay: 5000,
  });
  const timeout = setTimeout(
    () => socket.destroy(Error("CD stream handshake timed out")),
    5000,
  );
  try {
    await once(socket, "connect");
    socket.write(wire.CD_STREAM_REQUEST);
    for await (const record of decodeCdStream(socket)) {
      clearTimeout(timeout);
      yield record;
    }
  } finally {
    clearTimeout(timeout);
    socket.destroy();
  }
}
