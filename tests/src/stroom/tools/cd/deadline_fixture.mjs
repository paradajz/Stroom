import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
const setTimer = globalThis.setTimeout,
  clearTimer = globalThis.clearTimeout;
const timers = new Map(),
  starts = { recognition: 0, artwork: 0 };
const requests = [],
  aborted = [];
globalThis.setTimeout = (callback, ms, ...args) => {
  const timer = setTimer(callback, ms, ...args);
  if (ms === 60000 || ms === 120000) {
    const phase = ms === 60000 ? "recognition" : "artwork";
    timers.set(timer, { phase, expire: () => callback(...args) });
    ++starts[phase];
  }
  return timer;
};
globalThis.clearTimeout = (timer) => {
  timers.delete(timer);
  clearTimer(timer);
};
let pacing = 0;
Object.defineProperty(performance, "now", { value: () => pacing });
const firstId = discId({ first: 1, offsets: [150], leadout: 15000 });
function stalled(signal, name) {
  return new Promise((resolve, reject) => {
    const abort = () => {
      aborted.push(name);
      reject(signal.reason);
    };
    signal.addEventListener("abort", abort, { once: true });
    if (signal.aborted) abort();
    console.log("DEADLINE_STALL " + name);
  });
}
globalThis.fetch = async (address, { signal }) => {
  pacing += 5000;
  const url = new URL(address);
  requests.push(url.hostname + url.pathname);
  if (url.hostname === "musicbrainz.org") {
    if (url.pathname.endsWith(firstId)) return stalled(signal, "recognition");
    return Response.json({ releases: [] });
  }
  if (url.hostname === "gnudb.gnudb.org") return new Response("202 No match\n");
  if (url.hostname === "db.cue.tools") return new Response("<ctdb></ctdb>");
  if (url.hostname === "api.discogs.com") return Response.json({ results: [] });
  if (url.hostname === "limited.deadline.test")
    return new Response(null, {
      status: 429,
      headers: { "Retry-After": "3600" },
    });
  if (url.hostname === "slow.deadline.test")
    return new Response(
      new ReadableStream({
        start(controller) {
          signal.addEventListener(
            "abort",
            () => {
              aborted.push("artwork");
              controller.error(signal.reason);
            },
            { once: true },
          );
          console.log("DEADLINE_STALL artwork");
        },
      }),
      { headers: { "Content-Type": "image/jpeg" } },
    );
  if (url.hostname === "next.deadline.test")
    return new Response(null, { status: 404 });
  throw Error("Unexpected provider: " + address);
};
process.on("message", (message) => {
  if (message.expire)
    for (const [timer, entry] of [...timers]) {
      if (entry.phase !== message.expire) continue;
      clearTimeout(timer);
      entry.expire();
    }
  setImmediate(() =>
    process.send({
      requests,
      aborted,
      starts,
      active: [...timers.values()].map((item) => item.phase),
    }),
  );
});
process.on("disconnect", () => process.exit(1));
process.channel.unref();
