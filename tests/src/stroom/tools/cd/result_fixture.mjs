import fs from "node:fs/promises";
import { basename } from "node:path";
import { syncBuiltinESMExports } from "node:module";
import {
  ResultCache,
  RESULT_CACHE_BYTES,
} from "../../../../../tools/cd/recognition/report.mjs";
const open = fs.open;
const writeFile = fs.writeFile;
const reads = {};
const writes = new Set();
let gateWrites = false;
let memoryBytes = Number(
  process.env.RESULT_TEST_RAM_BYTES ?? RESULT_CACHE_BYTES,
);
const store = ResultCache.prototype.store;
ResultCache.prototype.store = function (...args) {
  this.maxBytes = memoryBytes;
  return store.apply(this, args);
};
fs.writeFile = async (path, ...args) => {
  if (gateWrites && String(path).endsWith(".json.tmp"))
    await new Promise((resolve) => writes.add(resolve));
  return writeFile(path, ...args);
};
fs.open = async (path, ...args) => {
  if (String(path).endsWith(".json")) {
    const name = basename(String(path));
    reads[name] = (reads[name] ?? 0) + 1;
  }
  return open(path, ...args);
};
syncBuiltinESMExports();
const timers = [];
const interval = globalThis.setInterval;
globalThis.setInterval = (callback, ms, ...args) => {
  if (ms === 60000) timers.push(callback);
  return interval(callback, ms, ...args);
};
const realNow = performance.now.bind(performance);
let pacing = 0,
  calls = 0,
  mode = "offline";
Object.defineProperty(performance, "now", { value: () => realNow() + pacing });
process.on("message", (message) => {
  if (message.mode) mode = message.mode;
  if (message.memoryBytes) memoryBytes = message.memoryBytes;
  if (message.gateWrites) gateWrites = true;
  if (message.releaseWrites) {
    gateWrites = false;
    for (const release of writes) release();
    writes.clear();
  }
  if (message.sample) for (const callback of timers) callback();
  process.send({ reads, calls, writes: writes.size });
});
process.on("disconnect", () => process.exit(1));
process.channel.unref();
globalThis.fetch = async (address) => {
  ++calls;
  pacing += 5000;
  const url = new URL(address);
  if (mode === "offline")
    throw Error("Cached results must not contact providers");
  if (mode === "limit") {
    if (url.hostname !== "musicbrainz.org")
      throw Error("Limit failures must stop the lookup");
    return Response.json({ releases: Array(65).fill({ id: "excess" }) });
  }
  if (url.hostname === "musicbrainz.org")
    return Response.json({
      diagnostic: "x".repeat(2 * 1024 * 1024),
      releases: [
        {
          id: "12345678-1234-1234-1234-123456789abc",
          title: "New album",
          "artist-credit": [{ name: "Artist" }],
          "cover-art-archive": { front: false },
          media: [
            { position: 1, tracks: [{ title: "New track", length: 198000 }] },
          ],
        },
      ],
    });
  if (url.hostname === "api.discogs.com") return Response.json({ results: [] });
  throw Error("Unexpected provider " + address);
};
