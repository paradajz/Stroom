import { coverInput } from "./cover_fixture.mjs";
import { discId } from "../../../../../tools/cd/disc.mjs";

const discs = [
  { first: 1, offsets: [150, 4650], leadout: 9150 },
  { first: 1, offsets: [150, 4725], leadout: 9300 },
];
const releaseId = (index) => `12345678-1234-1234-1234-12345678900${index}`;
const gates = new Map();
process.on("message", (message) => gates.get(message)?.());
process.channel.unref();
if (process.env.QUEUE_TEST_CLOCK) {
  const realNow = Date.now;
  let offset = 0;
  Date.now = () => realNow() + offset;
  process.on("message", (message) => {
    if (Number.isFinite(message?.advance)) {
      offset += message.advance;
      process.send("advanced");
    }
  });
}
const wait = (name) =>
  new Promise((resolve) => {
    gates.set(name, resolve);
    console.log(`QUEUE_WAIT ${name}`);
  });

globalThis.fetch = async (url) => {
  const address = String(url);
  if (address.includes("coverartarchive.org")) {
    const index = discs.findIndex((_, i) => address.includes(releaseId(i)));
    if (index < 0) throw Error(`Unexpected artwork request: ${address}`);
    await wait(`cover-${index}`);
    return new Response(coverInput, {
      headers: { "content-type": "image/png" },
    });
  }
  const index = discs.findIndex((toc) => address.includes(discId(toc)));
  if (index < 0) throw Error(`Unexpected recognition request: ${address}`);
  console.log(`QUEUE_LOOKUP ${index}`);
  if (
    process.env.QUEUE_BLOCK_RECOGNITION === "all" ||
    (index === 1 && process.env.QUEUE_BLOCK_RECOGNITION)
  )
    await wait(`recognition-${index}`);
  const toc = discs[index];
  return Response.json({
    releases: [
      {
        id: releaseId(index),
        title: `Album ${index}`,
        "artist-credit": [{ name: "Artist" }],
        media: [
          {
            position: 1,
            tracks: toc.offsets.map((offset, i) => ({
              number: String(i + 1),
              title: `Disc ${index} track ${i + 1}`,
              length:
                (((toc.offsets[i + 1] ?? toc.leadout) - offset) * 1000) / 75,
            })),
          },
        ],
      },
    ],
  });
};
