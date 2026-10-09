import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { join } from "node:path";
import { verifyDiscogs } from "../../../../../tools/cd/discogs.mjs";
import {
  downloadCover,
  fallbackCover,
} from "../../../../../tools/cd/cover.mjs";
import { coverInput, jpegSize } from "./cover_fixture.mjs";

const full = "https://i.discogs.com/full.jpg";
const thumbnail = "https://i.discogs.com/thumb.jpg";
const seed = { artist: "Artist", title: "Album", tracks: ["Song"] };
const release = {
  id: 123,
  title: seed.title,
  artists: [{ name: seed.artist }],
  formats: [{ name: "CD" }],
  images: [{ type: "primary", uri: full, uri150: thumbnail }],
  thumb: thumbnail,
  tracklist: [
    { title: "Song", position: "1", type_: "track", duration: "1:00" },
  ],
};
const raw = [
  { search: seed, response: { results: [{ id: 123 }] } },
  { release },
];
const directory = await mkdtemp("/tmp/stroom-discogs-covers-");
const options = { userAgent: "test", directory };
const originalFetch = globalThis.fetch;
const image = () =>
  new Response(coverInput, { headers: { "content-type": "image/png" } });
try {
  const { candidates } = await verifyDiscogs([seed], options, raw);
  assert.equal(candidates.length, 1);
  const template = candidates[0];
  assert.deepEqual(template.coverUrls, [full, thumbnail]);
  for (const failure of [
    "none",
    "missing",
    "oversized",
    "invalid",
    "timeout",
  ]) {
    const candidate = structuredClone(template);
    const requests = [];
    globalThis.fetch = async (url) => {
      requests.push(url);
      if (url === full) {
        if (failure === "missing") return new Response(null, { status: 404 });
        if (failure === "oversized")
          return new Response(Buffer.alloc(5 * 1024 * 1024 + 1));
        if (failure === "invalid")
          return new Response("not an image", {
            headers: { "content-type": "image/jpeg" },
          });
        if (failure === "timeout") throw Error("timeout");
      }
      return image();
    };
    await downloadCover(
      failure,
      [candidate],
      { releaseId: candidate.id },
      options,
    );
    assert.deepEqual(requests, failure === "none" ? [full] : [full, thumbnail]);
    assert.equal(candidate.coverUrl, failure === "none" ? full : thumbnail);
    assert.equal(candidate.coverAvailable, true);
    assert.equal(candidate.coverRetryable, false);
    assert.equal(candidate.coverError, undefined);
    assert.equal(candidate.coverRetryAt, undefined);
    assert.deepEqual(
      jpegSize(await readFile(join(directory, candidate.coverFile))),
      { width: 32, height: 16 },
    );
  }

  // Rebuilt fallback candidates retain permanent URL failures across probes/restarts.
  let report = {
    discId: "retry",
    toc: { offsets: [150], leadout: 4650 },
    selected: { releaseId: "mb", mediumPosition: 1 },
    candidates: [
      {
        id: "mb",
        ...seed,
        media: [
          { position: 1, tracks: [{ title: "Song", milliseconds: 60000 }] },
        ],
      },
    ],
    discogs: { raw },
  };
  let requests = [];
  globalThis.fetch = async (url) => {
    requests.push(url);
    if (url === full) return new Response(null, { status: 404 });
    throw Error("temporary thumbnail failure");
  };
  await fallbackCover(report, options);
  assert.deepEqual(requests, [full, thumbnail]);
  assert.equal(report.coverFallback.candidates[0].coverRetryable, true);
  report = JSON.parse(JSON.stringify(report));
  requests = [];
  globalThis.fetch = async (url) => {
    requests.push(url);
    return image();
  };
  await fallbackCover(report, options);
  assert.deepEqual(requests, [thumbnail]);
  assert.ok(report.candidates[0].coverFile);

  const missing = structuredClone(template);
  requests = [];
  globalThis.fetch = async (url) => {
    requests.push(url);
    return new Response(null, { status: 404 });
  };
  await downloadCover("missing", [missing], { releaseId: missing.id }, options);
  await downloadCover(
    "missing",
    [JSON.parse(JSON.stringify(missing))],
    { releaseId: missing.id },
    options,
  );
  assert.deepEqual(requests, [full, thumbnail]);
  assert.equal(missing.coverAvailable, false);
  assert.equal(missing.coverRetryable, false);

  // A thumbnail must not bypass a cooldown imposed by the full-size response.
  const limited = structuredClone(template);
  requests = [];
  globalThis.fetch = async (url) => {
    requests.push(url);
    return new Response(null, {
      status: 429,
      headers: { "retry-after": "300" },
    });
  };
  await downloadCover("limited", [limited], { releaseId: limited.id }, options);
  assert.deepEqual(requests, [full]);
  assert.equal(limited.coverRetryable, true);
  assert.ok(limited.coverRetryAt > Date.now() + 290000);
  console.log(
    "Discogs thumbnail fallback, persistent rejection and cooldown checks passed",
  );
} finally {
  globalThis.fetch = originalFetch;
  await rm(directory, { recursive: true, force: true });
}
