import { discId } from "../../../../../tools/cd/recognition/disc.mjs";

const discs = Array.from({ length: 16 }, (_, index) => ({
  first: 1,
  offsets: [150, 15363 + index],
  leadout: 32314 + index,
}));
const ids = discs.map(discId);
const gates = new Map();
let now = Date.now();
let pacing = 0;
Date.now = () => now;
Object.defineProperty(performance, "now", { value: () => pacing });
process.on("message", (message) => {
  if (message.advance) now += message.advance;
  if (Number.isInteger(message.release)) gates.get(message.release)?.();
  process.send("ack");
});
process.channel.unref();
globalThis.fetch = async (address) => {
  pacing += 5000;
  const url = new URL(address);
  if (url.hostname === "musicbrainz.org") {
    const index = ids.indexOf(url.pathname.split("/").at(-1));
    if (index < 0) throw Error("Unexpected fixture lookup: " + address);
    console.log("CALLER_LOOKUP " + index);
    await new Promise((resolve) => gates.set(index, resolve));
    if (index === 6) throw Error("Fixture lookup failure");
    return Response.json({ releases: [] });
  }
  if (url.hostname === "gnudb.gnudb.org") return new Response("202 No match\n");
  if (url.hostname === "db.cue.tools") return new Response("<ctdb></ctdb>");
  throw Error("Unexpected fixture provider: " + address);
};
