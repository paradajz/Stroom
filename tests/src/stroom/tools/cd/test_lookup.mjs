import { coverInput, assertCover } from "./cover_fixture.mjs";
import "./test_ranking.mjs";
import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { parseToc, discId } from "../../../../../tools/cd/disc.mjs";
import { metadataReply } from "../../../../../tools/cd/response.mjs";
import { downloadCover } from "../../../../../tools/cd/cover.mjs";
import { lookupDisc } from "../../../../../tools/cd/musicbrainz.mjs";

const toc = {
  type: "stroom-cd-toc",
  version: 1,
  generation: 7,
  first: 1,
  leadout: 95462,
  offsets: [150, 15363, 32314, 46592, 63414, 80489],
};
assert.equal(
  discId(parseToc(Buffer.from(JSON.stringify(toc)))),
  "49HHV7Eb8UKF3aQiNmu1GR8vKTY-",
);
for (const change of [
  { first: 0 },
  { offsets: [] },
  { offsets: [149] },
  { offsets: [150, 150] },
  { leadout: 450001 },
  { version: 2 },
  { generation: -1 },
])
  assert.throws(() =>
    parseToc(Buffer.from(JSON.stringify({ ...toc, ...change }))),
  );
assert.throws(() => parseToc(Buffer.alloc(1025)));
const id = "12345678-1234-1234-1234-123456789abc";
const release = {
  id,
  title: "Album",
  "artist-credit": [{ name: "Artist" }],
  "cover-art-archive": { front: true },
  media: [
    {
      position: 1,
      discs: [{ id: discId(toc) }],
      tracks: toc.offsets.map((offset, i) => ({
        number: String(i + 1),
        recording: {
          title: "Song",
          length: (((toc.offsets[i + 1] ?? toc.leadout) - offset) * 1000) / 75,
        },
      })),
    },
  ],
};
const directory = await mkdtemp(join(tmpdir(), "stroom-cd-lookup-"));
const original = globalThis.fetch;
try {
  const requests = [];
  globalThis.fetch = async (url, options) => {
    requests.push({ url, at: performance.now() });
    assert.match(options.headers["User-Agent"], /test/);
    if (url.startsWith("https://coverartarchive.org/"))
      return new Response(coverInput, {
        headers: { "content-type": "image/jpeg" },
      });
    const params = new URL(url).searchParams;
    assert.equal(params.get("cdstubs"), "no");
    assert.ok(
      !params.get("inc").split(/[+ ]/u).includes("discids"),
      "The discid endpoint rejects the release-only discids include",
    );
    return Response.json({
      releases: [
        release,
        { ...release, id: "22345678-1234-1234-1234-123456789abc" },
      ],
    });
  };
  const report = await lookupDisc(toc, {
    userAgent: "test/1",
    directory,
  });
  await downloadCover(report.discId, report.candidates, report.selected, {
    userAgent: "test/1",
    directory,
  });
  assert.equal(report.match, "exact");
  assert.equal(report.candidateCount, 2);
  assert.equal(report.selected.releaseId, id);
  assert.equal(report.selected.tiedCandidates, 2);
  assert.equal(
    requests.filter((r) => r.url.startsWith("https://coverartarchive.org/"))
      .length,
    1,
  );
  assert.equal(report.candidates[0].media[0].exactDisc, true);
  assert.equal(report.candidates[0].media[0].tracks[0].title, "Song");
  await assertCover(
    await readFile(join(directory, report.candidates[0].coverFile)),
  );
  for (let i = 1; i < requests.length; ++i) {
    const previous = requests
      .slice(0, i)
      .findLast(
        (item) =>
          new URL(item.url).hostname === new URL(requests[i].url).hostname,
      );
    if (previous) assert.ok(requests[i].at - previous.at >= 1000);
  }
  globalThis.fetch = async (url) =>
    new URL(url).pathname.endsWith("/-")
      ? Response.json({ releases: [release] })
      : new Response(null, { status: 404 });
  assert.equal(
    (await lookupDisc(toc, { userAgent: "test/1", directory })).match,
    "fuzzy",
  );
  let rejectedCoverRequests = 0;
  const wrongRelease = structuredClone(release);
  wrongRelease.media[0].discs = [];
  for (const track of wrongRelease.media[0].tracks)
    track.recording.length += 20159;
  globalThis.fetch = async (url) => {
    if (url.startsWith("https://coverartarchive.org/")) rejectedCoverRequests++;
    return new URL(url).pathname.endsWith("/-")
      ? Response.json({ releases: [wrongRelease] })
      : new Response(null, { status: 404 });
  };
  const rejected = await lookupDisc(toc, {
    userAgent: "test/1",
    directory,
  });
  assert.equal(rejected.match, "fuzzy");
  assert.equal(rejected.selected, null);
  assert.match(rejected.candidates[0].comparison.rejection, /duration/);
  assert.equal(rejectedCoverRequests, 0);
  assert.equal(
    metadataReply(
      rejected,
      { request: 1, track: 1, service: "127.0.0.1" },
      12890,
    ),
    null,
  );
  globalThis.fetch = async () => new Response(null, { status: 404 });
  assert.equal(
    (await lookupDisc(toc, { userAgent: "test/1", directory })).match,
    "none",
  );
  globalThis.fetch = async () =>
    Response.json({ error: "Invalid include parameter" }, { status: 400 });
  await assert.rejects(
    lookupDisc(toc, { userAgent: "test/1", directory }),
    /HTTP 400:.*Invalid include parameter/u,
  );
  globalThis.fetch = async () => new Response(null, { status: 503 });
  await assert.rejects(
    lookupDisc(toc, { userAgent: "test/1", directory }),
    /503/,
  );
  console.log(
    "PASS: reference Disc ID, invalid TOCs, exact/ambiguous/fuzzy/no matches, cover download, request pacing and API failures.",
  );
} finally {
  globalThis.fetch = original;
  await rm(directory, { recursive: true, force: true });
}
