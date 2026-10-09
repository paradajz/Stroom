import { coverInput } from "./cover_fixture.mjs";
const failureStatus = Number(process.env.COVER_FAILURE_STATUS);
let covers = 0;
if (failureStatus || process.env.RETRY_TEST) {
  const now = Date.now;
  let offset = 0;
  Date.now = () => now() + offset;
  process.on("message", (message) => {
    if (message === "advance" || Number.isFinite(message.advance)) {
      offset += message.advance ?? 61000;
      process.send("advanced");
    }
  });
  process.channel.unref();
}
// Exercise the live service with deterministic MusicBrainz and cover responses.
let calls = 0;
globalThis.fetch = async (url) => {
  console.log(`FIXTURE_REQUEST ${++calls}`);
  if (process.env.CACHE_OFFLINE)
    throw Error("Offline: unexpected provider request");
  if (String(url).includes("api.discogs.com"))
    return process.env.RETRY_DISCOGS
      ? new Response(null, { status: 429, headers: { "Retry-After": "180" } })
      : Response.json({ results: [] });
  if (String(url).includes("coverartarchive.org")) {
    if (failureStatus && covers++ === 0)
      return new Response(null, {
        status: failureStatus,
        headers: process.env.RETRY_AFTER
          ? { "Retry-After": process.env.RETRY_AFTER }
          : {},
      });
    console.log("FIXTURE_COVER_WAIT");
    if (!failureStatus && !process.env.RETRY_TEST)
      await new Promise((resolve) => process.once("message", resolve));
    return new Response(coverInput, {
      headers: { "content-type": "image/jpeg" },
    });
  }
  return Response.json({
    releases: [
      {
        id: "12345678-1234-1234-1234-123456789abc",
        title: "Fixture album",
        "artist-credit": [{ name: "Fixture artist" }],
        media: [
          {
            position: 1,
            tracks: [
              { number: "1", title: "First track", length: 202840 },
              { number: "2", title: "Second track", length: 226013 },
            ],
          },
        ],
      },
    ],
  });
};
