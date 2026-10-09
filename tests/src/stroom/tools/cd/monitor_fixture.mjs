import { JobDeadline } from "../../../../../tools/cd/recognition/deadline.mjs";
// Synthetic queue-age jumps exercise monitoring without consuming worker budgets.
// Real deadline timers and cancellation still apply.
JobDeadline.prototype.check = function () {
  this.signal.throwIfAborted();
};
import fs from "node:fs/promises";
import { syncBuiltinESMExports } from "node:module";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";

const originalWrite = fs.writeFile;
let failWrites = false;
let freeBytes = 8_000_000_000;
fs.statfs = async () => ({ bavail: freeBytes, bsize: 1 });
fs.writeFile = async (path, ...args) => {
  if (failWrites && String(path).endsWith(".json.tmp"))
    throw Object.assign(Error("Fixture disk full"), { code: "ENOSPC" });
  return originalWrite(path, ...args);
};
syncBuiltinESMExports();
const timers = [];
const originalInterval = globalThis.setInterval;
globalThis.setInterval = (callback, interval, ...args) => {
  if (interval === 60000) timers.push(callback);
  return originalInterval(callback, interval, ...args);
};
const epoch = Date.now();
let elapsed = 0;
let pacing = 0;
Date.now = () => epoch + elapsed;
Object.defineProperty(performance, "now", { value: () => elapsed + pacing });
const gates = new Map();
const discs = Array.from({ length: 4 }, (_, index) => ({
  first: 1,
  offsets: [150, 15363 + index],
  leadout: 32314 + index,
}));
const ids = discs.map(discId);
process.on("message", (message) => {
  if (message.advance) elapsed += message.advance;
  if (message.freeBytes !== undefined) freeBytes = message.freeBytes;
  if (message.failWrites !== undefined) failWrites = message.failWrites;
  if (message.release) gates.get(message.release)?.();
  if (message.sample) for (const callback of timers) callback();
  process.send("ack");
});
process.channel.unref();
globalThis.fetch = async (address) => {
  pacing += 5000;
  const url = new URL(address);
  if (url.hostname === "musicbrainz.org") {
    const index = ids.indexOf(url.pathname.split("/").at(-1));
    if (index < 0) throw Error("Unknown fixture disc");
    const key = "lookup-" + index;
    console.log("MONITOR_GATE " + key);
    await new Promise((resolve) => gates.set(key, resolve));
    if (index === 1) throw Error("Fixture provider unavailable");
    return Response.json({ releases: [] });
  }
  if (url.hostname === "images.test") {
    const key = "cover-" + url.pathname.slice(1);
    console.log("MONITOR_GATE " + key);
    await new Promise((resolve) => gates.set(key, resolve));
    return new Response(null, { status: 404 });
  }
  if (url.hostname === "gnudb.gnudb.org") return new Response("202 No match\n");
  if (url.hostname === "db.cue.tools") return new Response("<ctdb></ctdb>");
  if (url.hostname === "api.discogs.com") return Response.json({ results: [] });
  throw Error("Unexpected fixture provider: " + address);
};
