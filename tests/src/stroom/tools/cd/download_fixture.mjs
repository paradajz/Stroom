const originalNow = performance.now.bind(performance);
let pacing = 0;
Object.defineProperty(performance, "now", {
  value: () => originalNow() + pacing,
});
import fs from "node:fs/promises";
import http from "node:http";
import { syncBuiltinESMExports } from "node:module";

const readFile = fs.readFile;
const createServer = http.createServer;
const setTimer = globalThis.setTimeout;
const clearTimer = globalThis.clearTimeout;
const responses = new Set();
const deadlines = new Map();
const gates = new Set();
const writes = new Map();
const releasedWrites = new Set();
const samples = [];
const originalInterval = globalThis.setInterval;
globalThis.setInterval = (callback, interval, ...args) => {
  if (interval === 60000) samples.push(callback);
  return originalInterval(callback, interval, ...args);
};
let server,
  gateNext = false,
  slowNext = false,
  failNext = false;
let reads = 0,
  activeReads = 0,
  expired = 0,
  providerCalls = 0;
http.createServer = (options, listener) => {
  if (typeof options === "function") {
    listener = options;
    options = {};
  }
  server = createServer(options, (req, res) => {
    if (slowNext) {
      slowNext = false;
      // Delay completion callbacks after real TCP writes to model backpressure
      // deterministically, even when the loopback kernel buffers a whole JPEG.
      const socket = req.socket;
      const pending = new Set();
      const address = socket.remoteAddress;
      writes.set(address, pending);
      for (const method of ["_write", "_writev"]) {
        const write = socket[method];
        socket[method] = function (...args) {
          const callback = args.pop();
          return write.call(this, ...args, (...done) => {
            if (socket.destroyed || releasedWrites.has(address))
              callback(...done);
            else pending.add(() => callback(...done));
          });
        };
      }
      socket.once("close", () => {
        for (const finish of pending) finish();
        writes.delete(address);
        releasedWrites.delete(address);
      });
    }
    responses.add(res);
    res.once("finish", () => responses.delete(res));
    res.once("close", () => responses.delete(res));
    return listener(req, res);
  });
  return server;
};
fs.readFile = async (path, ...args) => {
  if (!String(path).endsWith(".jpg")) return readFile(path, ...args);
  ++reads;
  ++activeReads;
  try {
    if (failNext) {
      failNext = false;
      throw Object.assign(Error("Fixture cover read failure"), { code: "EIO" });
    }
    if (gateNext) {
      gateNext = false;
      await new Promise((resolve) => gates.add(resolve));
    }
    return await readFile(path, ...args);
  } finally {
    --activeReads;
  }
};
syncBuiltinESMExports();
globalThis.setTimeout = (callback, ms, ...args) => {
  const timer = setTimer(() => {
    deadlines.delete(timer);
    callback(...args);
  }, ms);
  if (ms === 30000 || ms === 5000)
    deadlines.set(timer, { ms, expire: () => callback(...args) });
  return timer;
};
globalThis.clearTimeout = (timer) => {
  deadlines.delete(timer);
  clearTimer(timer);
};
globalThis.fetch = async () => {
  ++providerCalls;
  throw Error("A cached cover download must not contact providers");
};
process.on("message", (message) => {
  if (message.advance) pacing += message.advance;
  if (message.slowNext) slowNext = true;
  if (message.releaseWrites) {
    releasedWrites.add(message.releaseWrites);
    const pending = writes.get(message.releaseWrites);
    if (pending) {
      for (const finish of pending) finish();
      pending.clear();
    }
  }
  if (message.sample) for (const sample of samples) sample();
  if (message.gateNext) gateNext = true;
  if (message.failNext) failNext = true;
  if (message.releaseReads) {
    for (const release of gates) release();
    gates.clear();
  }
  if (message.expire) {
    for (const [timer, entry] of [...deadlines]) {
      if (entry.ms !== (message.expire === "headers" ? 5000 : 30000)) continue;
      clearTimeout(timer);
      ++expired;
      entry.expire();
    }
  }
  setImmediate(() =>
    server.getConnections((error, connections) => {
      const images = [...responses].filter(
        (res) => res.headersSent && res.statusCode === 200 && !res.destroyed,
      );
      process.send({
        connections,
        openResponses: responses.size,
        images: images.length,
        queuedBytes: images.reduce((sum, res) => sum + res.writableLength, 0),
        reads,
        activeReads,
        gatedReads: gates.size,
        deadlines: [...deadlines.values()].filter((entry) => entry.ms === 30000)
          .length,
        headerDeadlines: [...deadlines.values()].filter(
          (entry) => entry.ms === 5000,
        ).length,
        expired,
        providerCalls,
      });
    }),
  );
});
process.on("disconnect", () => process.exit(1));
process.channel.unref();
