import { coverInput, assertCover } from "./cover_fixture.mjs";
import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { fallbackCover, resolveCover } from "../../../../../tools/cd/cover.mjs";
const directory = await mkdtemp("/tmp/stroom-cover-fallback-");
const tracks = Array.from({ length: 10 }, (_, i) => ({
  title: "Track " + (i + 1),
  number: String(i + 1),
  milliseconds: 60000,
}));
const originalReport = {
  discId: "test-disc",
  toc: { offsets: tracks.map((_, i) => 150 + i * 4500), leadout: 45150 },
  selected: { releaseId: "mb-release", mediumPosition: 1 },
  candidates: [
    {
      id: "mb-release",
      source: "musicbrainz",
      artist: "Artist",
      title: "Album",
      coverAvailable: false,
      media: [{ position: 1, tracks }],
    },
  ],
};
const original = globalThis.fetch;
let calls = 0,
  fail = false,
  duration = "1:00",
  wrongName = false;
globalThis.fetch = async (url) => {
  calls++;
  if (fail) throw Error("Discogs unavailable");
  if (String(url).includes("/database/search"))
    return Response.json({ results: [{ id: 123 }] });
  if (String(url).includes("/releases/"))
    return Response.json({
      title: "Album",
      artists: [{ name: "Artist" }],
      formats: [{ name: "CD" }],
      images: [{ type: "primary", uri150: "https://i.discogs.com/test.jpg" }],
      tracklist: tracks.map((t) => ({
        type_: "track",
        title: wrongName && t.number === "1" ? "Other song" : t.title,
        position: t.number,
        duration,
      })),
    });
  return new Response(coverInput, {
    headers: { "content-type": "image/jpeg" },
  });
};
const options = { userAgent: "stroom-test/1", directory };
try {
  let report = structuredClone(originalReport);
  await fallbackCover(report, options);
  const winner = report.candidates[0];
  assert.deepEqual(report.selected, originalReport.selected);
  assert.deepEqual(winner.media, originalReport.candidates[0].media);
  assert.equal(winner.id, "mb-release");
  assert.equal(winner.coverSource.releaseId, "discogs-123");
  await assertCover(await readFile(directory + "/" + winner.coverFile));
  const previous = calls;
  await fallbackCover(report, options);
  assert.equal(calls, previous, "Already downloaded artwork must be retained");
  for (const source of ["discogs", "musicbrainz"]) {
    report = structuredClone(originalReport);
    report.candidates[0].source = source;
    report.discogs = {
      raw: [
        {
          search: { artist: "Artist", title: "Album" },
          response: { results: [{ id: 456 }] },
        },
        {
          release: {
            id: 456,
            title: "Album",
            artists: [{ name: "Artist" }],
            formats: [{ name: "CD" }],
            images: [
              { type: "primary", uri150: "https://i.discogs.com/cached.jpg" },
            ],
            tracklist: tracks.map((t) => ({
              type_: "track",
              title: t.title,
              position: t.number,
              duration: "1:00",
            })),
          },
        },
      ],
    };
    const before = calls;
    await fallbackCover(report, options);
    assert.equal(
      calls - before,
      1,
      "Only download artwork; reuse fetched search and releases",
    );
    assert.equal(report.candidates[0].coverSource.releaseId, "discogs-456");
    const candidate = report.coverFallback.candidates[0];
    assert.equal(candidate.source, "discogs");
    assert.deepEqual(candidate.verification, {
      seedSource: source,
      seedId: "mb-release",
      agreeingTrackNames: tracks.length,
      trackCount: tracks.length,
    });
    assert.deepEqual(report.selected, originalReport.selected);
    assert.deepEqual(
      report.candidates[0].media,
      originalReport.candidates[0].media,
    );
  }
  fail = true;
  report = structuredClone(originalReport);
  await fallbackCover(report, options);
  assert.deepEqual(report.selected, originalReport.selected);
  assert.match(report.coverFallback.error, /unavailable/);
  await resolveCover(report, options);
  assert.equal(report.artworkRetryable, true);
  fail = false;
  await resolveCover(report, options);
  assert.equal(report.artworkRetryable, false);
  assert.ok(report.candidates[0].coverFile);
  for (const catalogDuration of ["1:08", ""]) {
    duration = catalogDuration;
    report = structuredClone(originalReport);
    await fallbackCover(report, options);
    assert.ok(report.candidates[0].coverFile);
    assert.equal(
      report.candidates[0].coverSource.matchBasis,
      "artist-album-all-track-names",
    );
    assert.ok(report.coverFallback.candidates[0].comparison.rejection);
    assert.deepEqual(report.selected, originalReport.selected);
    assert.deepEqual(
      report.candidates[0].media,
      originalReport.candidates[0].media,
    );
  }
  wrongName = true;
  report = structuredClone(originalReport);
  await fallbackCover(report, options);
  assert.equal(
    report.candidates[0].coverFile,
    undefined,
    "Without accepted timings, every track name must agree for artwork",
  );
  console.log(
    "PASS: cover-only fallback, unchanged metadata, cached artwork, failures and unrelated releases.",
  );
} finally {
  globalThis.fetch = original;
  await rm(directory, { recursive: true, force: true });
}
