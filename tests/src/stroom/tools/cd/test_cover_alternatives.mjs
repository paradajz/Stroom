import assert from "node:assert/strict";
import childProcess from "node:child_process";
import { syncBuiltinESMExports } from "node:module";
import { PassThrough } from "node:stream";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { resolveCover } from "../../../../../tools/cd/cover.mjs";
import { cachedReport, saveReport } from "../../../../../tools/cd/cache.mjs";
import { discId } from "../../../../../tools/cd/disc.mjs";

// These tests cover selection, persistence and retries. Pixel conversion has its
// own FFmpeg integration tests; keep this regression independent of that tool.
const originalExec = childProcess.execFile;
const originalFetch = globalThis.fetch;
const jpeg = Buffer.from([0xff, 0xd8, 0xff, 0xd9]);
childProcess.execFile = (command, args, options, callback) => {
  assert.equal(command, "ffmpeg");
  const stdin = new PassThrough();
  stdin.resume();
  stdin.on("finish", () => callback(null, jpeg, Buffer.alloc(0)));
  return { stdin };
};
syncBuiltinESMExports();
const directory = await mkdtemp(join(tmpdir(), "stroom-alternative-cover-"));
const options = { directory, userAgent: "test" };
const toc = { first: 1, offsets: [150, 4650], leadout: 9150 };
const winner = {
  id: "selected",
  source: "musicbrainz",
  title: "Album",
  artist: "Artist",
  coverAvailable: false,
  media: [
    {
      position: 1,
      tracks: [
        { title: "First", milliseconds: 60000 },
        { title: "Second", milliseconds: 60000 },
      ],
    },
  ],
};
function alternative(id = "alternative") {
  return {
    ...structuredClone(winner),
    id,
    coverAvailable: true,
    coverUrl: `https://${id}.example/cover`,
    url: `https://musicbrainz.org/release/${id}`,
  };
}
function report(...others) {
  return {
    discId: discId(toc),
    toc,
    selected: { releaseId: winner.id, mediumPosition: 1 },
    candidates: [structuredClone(winner), ...others],
  };
}
let calls = [];
let responses = new Map();
globalThis.fetch = async (url) => {
  calls.push(url);
  if (String(url).startsWith("https://api.discogs.com/"))
    return Response.json({ results: [] });
  const status = responses.get(url) ?? 200;
  return new Response(status === 200 ? "mock image" : "unavailable", {
    status,
    headers: { "content-type": "image/jpeg" },
  });
};
try {
  let result = report(alternative());
  const selected = structuredClone(result.selected);
  await resolveCover(result, options);
  assert.deepEqual(calls, [result.candidates[1].coverUrl]);
  assert.deepEqual(result.selected, selected);
  for (const key of ["id", "title", "artist", "media"])
    assert.deepEqual(result.candidates[0][key], winner[key]);
  assert.equal(result.candidates[0].coverSource.provider, "musicbrainz");
  assert.equal(result.candidates[0].coverSource.releaseId, "alternative");
  assert.deepEqual(
    await readFile(join(directory, result.candidates[0].coverFile)),
    jpeg,
  );
  assert.equal(result.artworkRetryable, false);
  await saveReport(directory, result.discId, result);
  const cached = await cachedReport(directory, result.discId);
  assert.equal(cached.candidates[0].coverFile, result.candidates[0].coverFile);
  calls = [];
  await resolveCover(cached, options);
  assert.deepEqual(calls, []);

  // The selected release still has priority when it provides an image.
  result = report(alternative());
  result.candidates[0].coverAvailable = true;
  result.candidates[0].coverUrl = "https://selected.example/cover";
  await resolveCover(result, options);
  assert.deepEqual(calls, [result.candidates[0].coverUrl]);

  // Structured credits match regardless of display join phrases or contributor order.
  for (const [names, accepted] of [
    [["Artist B", "Artist A"], true],
    [["Artist A"], false],
    [["Artist A", "Unrelated"], false],
  ]) {
    const other = alternative("multi-artist");
    other.artist = "Artist B & Artist A";
    other.artists = names;
    result = report(other);
    result.candidates[0].artist = "Artist A and Artist B";
    result.candidates[0].artists = ["Artist A", "Artist B"];
    calls = [];
    await resolveCover(result, options);
    assert.equal(Boolean(result.candidates[0].coverFile), accepted);
    if (accepted) assert.deepEqual(calls, [other.coverUrl]);
    else assert.ok(calls.every((url) => !url.includes("multi-artist.example")));
  }

  // Similar durations alone do not establish that artwork belongs to this album.
  for (const change of ["artist", "title", "tracks", "order", "count"]) {
    const other = alternative();
    if (change === "artist" || change === "title") other[change] = "Different";
    else if (change === "tracks")
      other.media[0].tracks[0].title = "Different song";
    else if (change === "order") other.media[0].tracks.reverse();
    else other.media[0].tracks.pop();
    calls = [];
    result = report(other);
    await resolveCover(result, options);
    assert.equal(calls.length, 2);
    assert.ok(
      calls.every((url) => new URL(url).hostname === "api.discogs.com"),
    );
    assert.equal(new URL(calls[0]).searchParams.get("artist"), "Artist");
    assert.equal(new URL(calls[1]).searchParams.has("artist"), false);
    assert.equal(result.candidates[0].coverFile, undefined);
  }

  // Matching alternate editions can have missing catalog timings.
  const other = alternative("untimed");
  other.artist = " artist ";
  other.title = "ÁLBUM";
  for (const track of other.media[0].tracks) delete track.milliseconds;
  calls = [];
  result = report(other);
  await resolveCover(result, options);
  assert.deepEqual(calls, [other.coverUrl]);

  // A permanent miss does not prevent trying the next existing candidate.
  const missing = alternative("missing"),
    next = alternative("next");
  responses.set(missing.coverUrl, 404);
  calls = [];
  result = report(missing, next);
  await resolveCover(result, options);
  assert.deepEqual(calls, [missing.coverUrl, next.coverUrl]);
  assert.equal(result.candidates[0].coverSource.releaseId, next.id);

  // A temporary alternative-cover failure must survive in the report for retry.
  const temporary = alternative("temporary");
  responses.set(temporary.coverUrl, 503);
  result = report(temporary);
  await resolveCover(result, options);
  assert.equal(result.artworkRetryable, true);
  responses.delete(temporary.coverUrl);
  calls = [];
  await resolveCover(result, options);
  assert.deepEqual(calls, [temporary.coverUrl]);
  assert.equal(result.artworkRetryable, false);
  assert.equal(result.candidates[0].coverSource.releaseId, temporary.id);
  // Exhausted existing candidates still fall through to the shared Discogs path.
  result = report(alternative("missing"));
  result.discogs = {
    raw: [
      {
        search: { artist: "Artist", title: "Album" },
        response: { results: [{ id: 123 }] },
      },
      {
        release: {
          id: 123,
          title: "Album",
          artists: [{ name: "Artist" }],
          formats: [{ name: "CD" }],
          images: [{ type: "primary", uri150: "https://i.discogs.com/cover" }],
          tracklist: winner.media[0].tracks.map((track, i) => ({
            type_: "track",
            title: track.title,
            position: String(i + 1),
            duration: "1:00",
          })),
        },
      },
    ],
  };
  calls = [];
  await resolveCover(result, options);
  assert.deepEqual(calls, [
    "https://missing.example/cover",
    "https://i.discogs.com/cover",
  ]);
  assert.equal(result.candidates[0].coverSource.provider, "discogs");
  assert.equal(result.candidates[0].coverSource.releaseId, "discogs-123");
  assert.deepEqual(result.selected, selected);
  assert.equal(result.artworkRetryable, false);
  // Real report: 12/13 names match; Discogs omits timings and has "Ramha".
  const fixture = JSON.parse(
    await readFile(
      new URL("./fixtures/one-to-one-cover.json", import.meta.url),
      "utf8",
    ),
  );
  calls = [];
  result = structuredClone(fixture);
  await resolveCover(result, options);
  assert.deepEqual(calls, ["https://i.discogs.com/one-to-one-test.jpg"]);
  assert.equal(
    result.candidates[0].coverSource.matchBasis,
    "artist-album-track-names-isolated-typo",
  );
  assert.deepEqual(result.selected, fixture.selected);
  assert.deepEqual(result.candidates[0].media, fixture.candidates[0].media);
  assert.equal(result.artworkRetryable, false);
  assert.equal(
    result.coverFallback.candidates[0].comparison.rejection,
    "Incomplete track timings",
  );

  for (const title of ["Rama Krishna Prabhu Tuu", "Rama Krishna Prabhu Ta"]) {
    result = structuredClone(fixture);
    for (const item of result.discogs.raw.filter((item) => item.release))
      item.release.tracklist[3].title = title;
    calls = [];
    await resolveCover(result, options);
    assert.equal(
      calls.length,
      1,
      "One insertion or substitution permits artwork",
    );
  }
  for (const change of [
    "unrelated",
    "two-typos",
    "order",
    "short-title",
    "short-disc",
  ]) {
    result = structuredClone(fixture);
    for (const item of result.discogs.raw.filter((item) => item.release)) {
      const list = item.release.tracklist;
      if (change === "unrelated") list[3].title = "Completely different song";
      if (change === "two-typos") list[2].title += "a";
      if (change === "order")
        [list[0].title, list[1].title] = [list[1].title, list[0].title];
      if (change === "short-title") {
        list[3].title = "Ramha";
        result.candidates[0].media[0].tracks[3].title = "Rama";
      }
      if (change === "short-disc") item.release.tracklist = list.slice(0, 9);
    }
    if (change === "short-disc") {
      result.candidates[0].media[0].tracks.length = 9;
      result.toc.offsets.length = 9;
    }
    calls = [];
    await resolveCover(result, options);
    assert.equal(result.candidates[0].coverFile, undefined, change);
    assert.deepEqual(calls, [], "Rejected artwork must not be downloaded");
  }
  console.log(
    "PASS: existing cover candidates, metadata preservation, cache, rejection and retries",
  );
} finally {
  globalThis.fetch = originalFetch;
  childProcess.execFile = originalExec;
  syncBuiltinESMExports();
  await rm(directory, { recursive: true, force: true });
}
