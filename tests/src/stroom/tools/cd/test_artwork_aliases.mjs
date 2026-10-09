import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { resolveCover } from "../../../../../tools/cd/recognition/cover.mjs";
import { verifyDiscogs } from "../../../../../tools/cd/recognition/discogs.mjs";
import { coverInput, assertCover } from "./cover_fixture.mjs";

// Pruned metadata: https://api.discogs.com/releases/12674441. Image bytes are local.
const release = JSON.parse(
  await readFile(new URL("./fixtures/deep-space-iv.json", import.meta.url)),
);
const titles = release.tracklist.map((track) => track.title);
titles[13] = "Devil´s Corner";
titles[15] = "Buenos Aires (Terry Lee Brown Jr. Remix)";
const seed = {
  source: "gnudb",
  id: "gnudb-test",
  title: "Deep Space Vol. 4 (A Trip From Deephouse To Techhouse)",
  artist: "George Perry Pres.",
  artists: ["George Perry Pres."],
  tracks: titles,
};
const toc = {
  first: 1,
  offsets: titles.map((_, i) => 150 + i * 4500),
  leadout: 150 + titles.length * 4500,
};
function report() {
  return {
    discId: "deep-space-test",
    toc,
    selected: { releaseId: seed.id, mediumPosition: 1 },
    candidates: [
      {
        ...seed,
        media: [
          {
            position: 1,
            tracks: titles.map((title) => ({ title, milliseconds: 60000 })),
          },
        ],
      },
    ],
  };
}
const directory = await mkdtemp("/tmp/stroom-artwork-aliases-");
const originalFetch = globalThis.fetch;
const originalNow = Object.getOwnPropertyDescriptor(performance, "now");
let clock = 0;
Object.defineProperty(performance, "now", {
  configurable: true,
  value: () => (clock += 4000),
});
const calls = [];
let candidate = release;
let misleadingSearch = false;
globalThis.fetch = async (url) => {
  const parsed = new URL(url);
  calls.push(parsed);
  if (parsed.pathname === "/database/search") {
    const title = parsed.searchParams.get("release_title");
    return Response.json({
      results:
        title === "Deep Space IV"
          ? [{ id: candidate.id }]
          : misleadingSearch
            ? [{ id: 999 }]
            : [],
    });
  }
  if (parsed.pathname === "/releases/12674441") return Response.json(candidate);
  if (parsed.pathname === "/releases/999")
    return Response.json({ ...release, id: 999, title: "Unrelated album" });
  assert.equal(String(url), release.images[0].uri);
  return new Response(coverInput, { headers: { "content-type": "image/png" } });
};
const options = { directory, userAgent: "test" };
const cached = (value = release, input = seed) => [
  { search: input, response: { results: [{ id: value.id }] } },
  { release: value },
];
try {
  const result = report();
  const original = structuredClone(result);
  await resolveCover(result, options);
  assert.ok(
    result.candidates[0].coverFile,
    "Naming aliases must recover this cover",
  );
  assert.equal(result.candidates[0].coverSource.releaseId, "discogs-12674441");
  assert.equal(
    result.candidates[0].coverSource.matchBasis,
    "artist-album-track-name-aliases",
  );
  assert.equal(
    result.coverFallback.candidates[0].verification.agreeingTrackNames,
    15,
  );
  assert.equal(
    result.coverFallback.candidates[0].verification.agreeingArtworkTrackNames,
    16,
  );
  assert.equal(
    result.coverFallback.candidates[0].comparison.missingDurations,
    16,
  );
  assert.deepEqual(result.selected, original.selected);
  assert.deepEqual(result.candidates[0].media, original.candidates[0].media);
  assert.equal(result.candidates[0].title, seed.title);
  assert.equal(result.candidates[0].artist, seed.artist);
  await assertCover(
    await readFile(directory + "/" + result.candidates[0].coverFile),
  );
  const searches = calls.filter((url) => url.pathname === "/database/search");
  assert.ok(searches.length <= 4);
  assert.equal(
    searches.at(-1).searchParams.get("release_title"),
    "Deep Space IV",
  );

  // Cached metadata searches do not suppress artwork title variants.
  calls.length = 0;
  const stale = report();
  stale.coverFallback = {
    raw: result.coverFallback.raw.filter(
      (item) => item.search && !item.artworkQuery,
    ),
  };
  await resolveCover(stale, options);
  assert.ok(stale.candidates[0].coverFile);
  const freshSearches = calls.filter(
    (url) => url.pathname === "/database/search",
  );
  assert.equal(freshSearches.length, 2);
  assert.ok(
    freshSearches.every((url) =>
      ["Deep Space 4", "Deep Space IV"].includes(
        url.searchParams.get("release_title"),
      ),
    ),
  );

  // Saved new queries and fetched releases are reusable on an image retry.
  calls.length = 0;
  const retry = report();
  retry.coverFallback = result.coverFallback;
  await resolveCover(retry, options);
  assert.ok(retry.candidates[0].coverFile);
  assert.ok(!calls.some((url) => url.hostname === "api.discogs.com"));

  for (const change of [
    "volume",
    "invalid-roman",
    "artist",
    "extra-artist",
    "album",
    "track",
    "order",
    "count",
    "mix",
    "radio",
  ]) {
    candidate = structuredClone(release);
    if (change === "volume")
      candidate.title = "Deep Space V - From Deephouse To Techhouse";
    if (change === "invalid-roman")
      candidate.title = "Deep Space IIV - From Deephouse To Techhouse";
    if (change === "artist") candidate.artists = [{ name: "Unrelated DJ" }];
    if (change === "extra-artist")
      candidate.artists.push({ name: "Another DJ" });
    if (change === "album")
      candidate.title = "Another Space IV - From Deephouse To Techhouse";
    if (change === "track") candidate.tracklist[5].title = "Different song";
    if (change === "order")
      [candidate.tracklist[0].title, candidate.tracklist[1].title] = [
        candidate.tracklist[1].title,
        candidate.tracklist[0].title,
      ];
    if (change === "count") candidate.tracklist.pop();
    if (change === "mix")
      candidate.tracklist[15].title =
        "Buenos Aires (Terry Lee Brown Junior Dub)";
    if (change === "radio") candidate.tracklist[1].title += " (Radio Edit)";
    const invalid = report();
    invalid.discogs = { raw: cached(candidate) };
    calls.length = 0;
    await resolveCover(invalid, options);
    assert.equal(invalid.candidates[0].coverFile, undefined, change);
    assert.ok(!calls.some((url) => url.hostname === "i.discogs.com"), change);
  }
  candidate = release;
  // Naming aliases need every ordered name, including on singles and short albums.
  for (const count of [1, 6, 9]) {
    const indices = [...Array.from({ length: count - 1 }, (_, i) => i), 15];
    const shortRelease = structuredClone(release);
    shortRelease.tracklist = indices.map((index, i) => ({
      ...release.tracklist[index],
      position: String(i + 1),
    }));
    const template = report();
    template.candidates[0].media[0].tracks = indices.map((index) => ({
      title: titles[index],
      milliseconds: 60000,
    }));
    template.toc = {
      first: 1,
      offsets: toc.offsets.slice(0, count),
      leadout: 150 + count * 4500,
    };
    for (const change of [
      "matching",
      "track",
      "mix",
      "count",
      ...(count > 1 ? ["order"] : []),
    ]) {
      const value = structuredClone(shortRelease);
      if (change === "track") value.tracklist[0].title = "Different song";
      if (change === "mix")
        value.tracklist.at(-1).title =
          "Buenos Aires (Terry Lee Brown Junior Dub)";
      if (change === "count") value.tracklist.pop();
      if (change === "order")
        [value.tracklist[0].title, value.tracklist[1].title] = [
          value.tracklist[1].title,
          value.tracklist[0].title,
        ];
      const short = structuredClone(template);
      short.discogs = { raw: cached(value) };
      calls.length = 0;
      await resolveCover(short, options);
      if (change === "matching") {
        assert.ok(
          short.candidates[0].coverFile,
          `All ${count} tracks match after aliases`,
        );
        assert.equal(
          short.coverFallback.candidates[0].verification
            .agreeingArtworkTrackNames,
          count,
        );
        assert.deepEqual(short.selected, template.selected);
        assert.deepEqual(
          short.candidates[0].media,
          template.candidates[0].media,
        );
      } else {
        assert.equal(
          short.candidates[0].coverFile,
          undefined,
          `${count} tracks: ${change}`,
        );
        assert.ok(
          !calls.some((url) => url.hostname === "i.discogs.com"),
          `${count} tracks: ${change}`,
        );
      }
    }
  }

  misleadingSearch = true;
  calls.length = 0;
  const misleading = report();
  await resolveCover(misleading, options);
  assert.ok(
    misleading.candidates[0].coverFile,
    "Unrelated full-title hits must not stop core-title discovery",
  );
  assert.ok(
    calls.filter((url) => url.pathname.startsWith("/releases/")).length <= 8,
  );

  // Artwork aliases never loosen metadata recognition, even with complete timings.
  calls.length = 0;
  const timed = structuredClone(release);
  for (const track of timed.tracklist) track.duration = "1:00";
  assert.equal(
    (await verifyDiscogs([seed], options, cached(timed))).candidates.length,
    0,
  );
} finally {
  globalThis.fetch = originalFetch;
  if (originalNow) Object.defineProperty(performance, "now", originalNow);
  else delete performance.now;
  await rm(directory, { recursive: true, force: true });
}
console.log(
  "PASS: bounded artwork discovery, Roman volumes, presenter and Junior aliases, full ordered tracks and unchanged metadata selection.",
);
