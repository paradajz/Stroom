import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { resolveCover } from "../../../../../tools/cd/cover.mjs";
import { coverInput, assertCover } from "./cover_fixture.mjs";

// Pruned Discogs release 29505. Source: https://api.discogs.com/releases/29505
const release = JSON.parse(
  await readFile(new URL("./fixtures/techno-club-9.json", import.meta.url)),
);
const titles = [
  "Traumwelten",
  "The Rain",
  "Into My Brain (Plug’n’Play club mix)",
  "Genuine Draft",
  "Grave Diggers Have More Fun",
  "Sturm & Drang",
  "Full Moon",
  "Technotrain",
  "Relieve My Pain",
  "Hear You Calling (Dark Moon remix)",
  "Children of Paradise (DJ Shah mix)",
  "Answer Mother Earth (Void Vision mix)",
  "! Keep It That Way ! (DJ Pulsedriver mix)",
  "Pornostar (club mix)",
];
const tracks = release.tracklist.slice(0, 14).map((track, i) => {
  const [minutes, seconds] = track.duration.split(":").map(Number);
  return { title: titles[i], milliseconds: (minutes * 60 + seconds) * 1000 };
});
let leadout = 150;
const offsets = tracks.map((track) => {
  const start = leadout;
  leadout += (track.milliseconds * 75) / 1000;
  return start;
});
const originalReport = {
  discId: "techno-club-test",
  toc: { offsets, leadout },
  selected: {
    releaseId: "0d6bb929-8cfb-41a1-acfc-c2d756914f86",
    mediumPosition: 1,
  },
  candidates: [
    {
      id: "0d6bb929-8cfb-41a1-acfc-c2d756914f86",
      source: "musicbrainz",
      artist: "Talla 2XLC welcomes Yves de Ruyter",
      artists: ["Talla 2XLC", "Yves de Ruyter"],
      title: "Techno Club, Volume 9",
      coverAvailable: true,
      coverUrl:
        "https://coverartarchive.org/release/0d6bb929-8cfb-41a1-acfc-c2d756914f86/front-250",
      media: [{ position: 1, tracks }],
    },
  ],
};
const directory = await mkdtemp("/tmp/stroom-cover-timeout-");
const originalFetch = globalThis.fetch;
const originalNow = Object.getOwnPropertyDescriptor(performance, "now");
let clock = 0;
Object.defineProperty(performance, "now", {
  configurable: true,
  value: () => (clock += 4000),
});
let calls = [],
  variant = "matching";
globalThis.fetch = async (url) => {
  const parsed = new URL(url);
  calls.push(parsed);
  if (parsed.hostname === "coverartarchive.org")
    throw new DOMException(
      "The operation was aborted due to timeout",
      "TimeoutError",
    );
  if (parsed.pathname === "/database/search") {
    if (parsed.searchParams.has("artist"))
      return Response.json({ results: [] });
    assert.match(parsed.searchParams.get("release_title"), /9/);
    assert.doesNotMatch(parsed.searchParams.get("release_title"), /volume/i);
    return Response.json({ results: [{ id: 29505 }] });
  }
  if (parsed.pathname === "/releases/29505") {
    const value = structuredClone(release);
    if (variant === "wrong-volume") value.title = "Techno Club Vol.10";
    if (variant === "wrong-artists") value.artists = [{ name: "Unrelated DJ" }];
    return Response.json(value);
  }
  assert.equal(url, release.images[0].uri150);
  return new Response(coverInput, {
    headers: { "content-type": "image/jpeg" },
  });
};
try {
  const report = structuredClone(originalReport);
  await resolveCover(report, { directory, userAgent: "test" });
  assert.deepEqual(
    calls.map((url) => url.hostname),
    [
      "coverartarchive.org",
      "api.discogs.com",
      "api.discogs.com",
      "api.discogs.com",
      "i.discogs.com",
    ],
  );
  const winner = report.candidates[0];
  assert.equal(winner.coverSource.provider, "discogs");
  assert.equal(report.artworkRetryable, false);
  assert.equal(winner.coverError, undefined);
  assert.deepEqual(winner.media, originalReport.candidates[0].media);
  assert.deepEqual(report.selected, originalReport.selected);
  await assertCover(await readFile(`${directory}/${winner.coverFile}`));
  // Both search variants and release data are reusable after an image retry.
  calls = [];
  const cached = structuredClone(originalReport);
  cached.coverFallback = report.coverFallback;
  await resolveCover(cached, { directory, userAgent: "test" });
  assert.deepEqual(
    calls.map((url) => url.hostname),
    ["coverartarchive.org", "i.discogs.com"],
  );
  for (variant of ["wrong-volume", "wrong-artists"]) {
    calls = [];
    const rejected = structuredClone(originalReport);
    await resolveCover(rejected, { directory, userAgent: "test" });
    assert.equal(rejected.candidates[0].coverFile, undefined, variant);
    assert.equal(rejected.artworkRetryable, true);
    assert.ok(!calls.some((url) => url.hostname === "i.discogs.com"));
  }
} finally {
  globalThis.fetch = originalFetch;
  if (originalNow) Object.defineProperty(performance, "now", originalNow);
  else delete performance.now;
  await rm(directory, { recursive: true, force: true });
}
console.log(
  "PASS: timeout triggers Discogs fallback, volume aliases, cached search variants, and wrong-volume/artist rejection",
);
