import assert from "node:assert/strict";
import { lookupDisc } from "../../../../../tools/cd/recognition/musicbrainz.mjs";
import { lookupCtdb } from "../../../../../tools/cd/recognition/ctdb.mjs";
import { verifyDiscogs } from "../../../../../tools/cd/recognition/discogs.mjs";
import {
  MAX_CANDIDATES,
  MAX_MEDIA,
} from "../../../../../tools/cd/recognition/report.mjs";

const fetch = globalThis.fetch;
const realNow = performance.now.bind(performance);
let pacing = 0;
Object.defineProperty(performance, "now", {
  configurable: true,
  value: () => realNow() + pacing,
});
const toc = { first: 1, offsets: [150], leadout: 15150 };
const options = { userAgent: "test" };
const release = {
  id: "release",
  title: "Album",
  "artist-credit": [{ name: "Artist" }],
  media: [{ position: 1, tracks: [{ title: "One", length: 200000 }] }],
};
function respond(value) {
  globalThis.fetch = async () => {
    pacing += 5000;
    return typeof value === "string"
      ? new Response(value)
      : Response.json(value);
  };
}
try {
  respond({ releases: Array(MAX_CANDIDATES).fill(release) });
  assert.equal(
    (await lookupDisc(toc, options)).candidates.length,
    MAX_CANDIDATES,
  );
  for (const response of [
    { releases: Array(MAX_CANDIDATES + 1).fill(release) },
    {
      releases: [
        { ...release, media: Array(MAX_MEDIA + 1).fill(release.media[0]) },
      ],
    },
    {
      releases: [
        {
          ...release,
          media: [{ position: 1, tracks: Array(100).fill({ title: "One" }) }],
        },
      ],
    },
  ]) {
    respond(response);
    await assert.rejects(
      lookupDisc(toc, options),
      (error) => error.code === "CD_REPORT_LIMIT",
    );
  }
  respond(
    "<ctdb><metadata artist='Artist' album='Album'>" +
      "<track name='One'/>".repeat(100) +
      "</metadata></ctdb>",
  );
  await assert.rejects(lookupCtdb(toc, options), /exceeds 99 tracks/);
  respond(
    "<ctdb>" +
      "<metadata artist='Artist' album='Album'><track name='One'/></metadata>".repeat(
        20,
      ) +
      "</ctdb>",
  );
  assert.equal((await lookupCtdb(toc, options)).candidates.length, 5);

  const seed = { artist: "Artist", title: "Album", tracks: ["One"] };
  await assert.rejects(
    verifyDiscogs(Array(6).fill(seed), options),
    /search state/,
  );
  await assert.rejects(
    verifyDiscogs([seed], options, Array(33).fill({})),
    /search state/,
  );
  let calls = 0;
  globalThis.fetch = async (url) => {
    ++calls;
    pacing += 5000;
    if (url.includes("database/search"))
      return Response.json({
        results: Array.from({ length: 8 }, (_, index) => ({ id: index + 1 })),
      });
    return Response.json({
      id: Number(url.split("/").at(-1)),
      title: "Album",
      artists: [{ name: "Artist" }],
      formats: [{ name: "CD" }],
      tracklist: Array.from({ length: 9 }, (_, index) => ({
        type_: "track",
        position: index + 1 + ".1",
        title: "One",
        duration: "3:20",
      })),
    });
  };
  await assert.rejects(verifyDiscogs([seed], options), /exceeds 64 candidates/);
  assert.equal(calls, 9);

  let fetchedReleases = 0;
  globalThis.fetch = async (url) => {
    pacing += 5000;
    if (url.includes("database/search"))
      return Response.json({
        results: Array.from({ length: 8 }, (_, index) => ({ id: index + 1 })),
      });
    ++fetchedReleases;
    return Response.json({
      id: fetchedReleases,
      title: "Album",
      artists: [{ name: "Artist" }],
      formats: [{ name: "CD" }],
      diagnostic: "x".repeat(3 * 1024 * 1024),
      tracklist: [
        { type_: "track", position: "1", title: "One", duration: "3:20" },
      ],
    });
  };
  await assert.rejects(
    verifyDiscogs([seed], options),
    /exceeds 16777216 bytes/,
  );
  assert.equal(
    fetchedReleases,
    6,
    "Stop accumulating raw responses as soon as the report byte limit is reached",
  );
  console.log(
    "PASS: provider release/media/track/candidate limits, bounded CTDB parsing and cumulative diagnostic report size.",
  );
} finally {
  globalThis.fetch = fetch;
  Object.defineProperty(performance, "now", { value: realNow });
}
