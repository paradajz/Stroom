let now = Number(process.env.LOOKUP_TEST_NOW);
let mode = process.env.LOOKUP_TEST_MODE;
const calls = [];
Date.now = () => now;
// Advance provider pacing per request; wall time independently controls cache expiry.
let pacing = 0;
Object.defineProperty(performance, "now", { value: () => pacing });
process.on("message", (message) => {
  if (Number.isSafeInteger(message.now)) now = message.now;
  if (message.mode) mode = message.mode;
  process.send({ now, mode, calls });
});
process.channel.unref();

globalThis.fetch = async (address) => {
  pacing += 5000;
  const url = new URL(address);
  calls.push(url.hostname);
  const limited = () =>
    new Response(null, {
      status: 429,
      headers: {
        "Retry-After": process.env.LOOKUP_TEST_RETRY_SECONDS ?? "3600",
      },
    });
  if (url.hostname === "musicbrainz.org") {
    if (mode === "failure") throw Error("Fixture provider unavailable");
    if (mode === "limited") return limited();
    if (mode === "malformed") return Response.json({ unexpected: true });
    if (mode !== "recovered" && mode !== "rejected")
      return Response.json({ releases: [] });
    return Response.json({
      releases: [
        {
          id: "recovered",
          title: "Recovered album",
          "artist-credit": [{ name: "Artist" }],
          "cover-art-archive": { front: false },
          media: [
            {
              position: 1,
              tracks: [
                {
                  number: "1",
                  title: "First track",
                  length: mode === "rejected" ? 5000 : 202840,
                },
                {
                  number: "2",
                  title: "Second track",
                  length: mode === "rejected" ? 5000 : 226013,
                },
              ],
            },
          ],
        },
      ],
    });
  }
  if (url.hostname === "gnudb.gnudb.org") {
    if (mode === "gnudb-read-limited")
      return url.searchParams.get("cmd").startsWith("cddb read")
        ? limited()
        : new Response("200 data abcdef01 Artist / Album\n");
    return new Response("202 No match\n");
  }
  if (url.hostname === "db.cue.tools")
    return new Response(
      mode === "discogs-limited"
        ? '<ctdb><metadata artist="Artist" album="Album" source="freedb" id="test"><track name="A"/><track name="B"/></metadata></ctdb>'
        : "<ctdb></ctdb>",
    );
  if (url.hostname === "api.discogs.com")
    return mode === "discogs-limited"
      ? limited()
      : Response.json({ results: [] });
  throw Error("Unexpected fixture request: " + address);
};
