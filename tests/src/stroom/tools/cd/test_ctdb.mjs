import { cd } from "../../../../../tools/contracts/load.mjs";
import { coverInput, assertCover } from "./cover_fixture.mjs";
import assert from "node:assert/strict";
import { readFile, mkdtemp, rm } from "node:fs/promises";
import { lookupDisc } from "../../../../../tools/cd/lookup.mjs";
import { verifyDiscogs } from "../../../../../tools/cd/discogs.mjs";
import { lookupCtdb } from "../../../../../tools/cd/ctdb.mjs";
import { rankCandidates } from "../../../../../tools/cd/ranking.mjs";
import { metadataReply } from "../../../../../tools/cd/response.mjs";

const { toc, release } = JSON.parse(
  await readFile(new URL("./fixtures/uzmi-sve.json", import.meta.url)),
);
const xml = await readFile(
  new URL("./fixtures/uzmi-sve.xml", import.meta.url),
  "utf8",
);
const directory = await mkdtemp("/tmp/stroom-cover-test-");
release.images = [
  { type: "primary", uri150: "https://i.discogs.com/test-cover.jpg" },
];
const fixtureTracks = release.tracklist.filter(
  (track) => track.type_ === "track",
);
fixtureTracks[0].artists = [
  { name: "Guest Artist (2)" },
  { name: "Second Guest (12)" },
];
delete fixtureTracks[1].artists;
fixtureTracks[2].artists = [];
fixtureTracks[3].artists = [{ name: "Solo Guest (3)" }];
const original = globalThis.fetch;
let body = xml,
  reads = 0;
globalThis.fetch = async (url) => {
  const parsed = new URL(url);
  if (parsed.hostname === "musicbrainz.org")
    return new Response(null, { status: 404 });
  if (parsed.hostname === "db.cue.tools") {
    assert.equal(
      parsed.searchParams.get("toc"),
      [...toc.offsets, toc.leadout].map((v) => v - 150).join(":"),
    );
    assert.equal(parsed.searchParams.get("ctdb"), "0");
    return new Response(body);
  }
  if (parsed.hostname === "i.discogs.com")
    return new Response(coverInput, {
      headers: { "content-type": "image/jpeg" },
    });
  assert.equal(parsed.hostname, "api.discogs.com", "Unexpected provider");
  if (parsed.pathname === "/database/search")
    return Response.json({
      results: [{ id: release.id }, { id: 999999 }, { id: 999998 }],
    });
  reads++;
  if (parsed.pathname.endsWith("/999999"))
    throw Error("Release temporarily unavailable");
  return Response.json({
    ...release,
    id: Number(parsed.pathname.split("/").at(-1)),
    images: parsed.pathname.endsWith("/999998") ? release.images : [],
  });
};
try {
  const report = await lookupDisc(toc, {
    userAgent: "stroom-test/1",
    directory,
  });
  assert.equal(report.match, "ctdb-discogs");
  const recognized = report.candidates[0];
  assert.equal(recognized.source, "discogs");
  assert.ok(
    report.ctdb.candidates.some(
      (seed) =>
        seed.source === recognized.verification.seedSource &&
        seed.id === recognized.verification.seedId,
    ),
  );
  assert.equal("ctdbSource" in recognized.verification, false);
  assert.equal("ctdbId" in recognized.verification, false);
  await assertCover(
    await readFile(directory + "/" + report.candidates[0].coverFile),
  );
  assert.equal(report.selected.releaseId, "discogs-2228708");
  assert.equal(report.candidates[0].coverSource.releaseId, "discogs-999998");
  assert.deepEqual(report.selected.timingOutliers, [17]);
  assert.equal(report.candidates[0].verification.agreeingTrackNames, 18);
  assert.equal(reads, 3, "Continue fetching after a failed release");
  assert.deepEqual(report.candidates.map((c) => c.id).sort(), [
    "discogs-2228708",
    "discogs-999998",
  ]);
  assert.deepEqual(report.discogs.errors, [
    {
      releaseId: 999999,
      message: "Release temporarily unavailable",
      retryable: true,
    },
  ]);
  const reply = metadataReply(
    report,
    { request: 1, track: 1, service: "127.0.0.1" },
    12890,
  );
  assert.equal(
    JSON.parse(reply.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES)).data.title,
    "Samo Šuti",
  );
  assert.equal(
    JSON.parse(reply.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES)).data.artist,
    "Guest Artist, Second Guest",
  );
  for (const [track, artist] of [
    [2, report.candidates[0].artist],
    [3, report.candidates[0].artist],
    [4, "Solo Guest"],
  ]) {
    const packet = metadataReply(
      report,
      { request: track, track, service: "127.0.0.1" },
      12890,
    );
    assert.equal(
      JSON.parse(packet.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES)).data.artist,
      artist,
    );
  }
  const candidate = structuredClone(report.candidates[0]);
  candidate.media[0].corroboratedNames = false;
  assert.equal(
    rankCandidates(toc, [candidate]),
    null,
    "No exception without independent name agreement",
  );
  candidate.media[0].corroboratedNames = true;
  candidate.media[0].tracks[0].milliseconds += 10000;
  assert.equal(
    rankCandidates(toc, [candidate]),
    null,
    "Two outliers must fail",
  );
  candidate.media[0].tracks[0].milliseconds = null;
  assert.equal(
    rankCandidates(toc, [candidate]),
    null,
    "Missing duration must fail",
  );
  body =
    '<!DOCTYPE ctdb [<!ENTITY x SYSTEM "file:///etc/passwd">]><ctdb></ctdb>';
  await assert.rejects(lookupCtdb(toc, { userAgent: "test" }), /Invalid CTDB/);
  body =
    '<ctdb><metadata artist="x" album="y"><track name="&unknown;"/></metadata></ctdb>';
  await assert.rejects(lookupCtdb(toc, { userAgent: "test" }), /entity/);
  body = "<ctdb></ctdb>";
  assert.deepEqual(
    (await lookupCtdb(toc, { userAgent: "test" })).candidates,
    [],
  );
  const seed = {
    artist: release.artists[0].name,
    title: release.title,
    tracks: release.tracklist
      .filter((t) => t.type_ === "track")
      .map((t) => t.title),
  };
  let searches = 0;
  globalThis.fetch = async (url) => {
    if (String(url).includes("/database/search")) {
      searches++;
      if (searches === 1) throw Error("Search temporarily unavailable");
      return Response.json({ results: [{ id: release.id }] });
    }
    return Response.json(release);
  };
  const recovered = await verifyDiscogs([seed, seed], { userAgent: "test" });
  assert.equal(recovered.candidates.length, 1);
  assert.equal(recovered.errors[0].message, "Search temporarily unavailable");
  globalThis.fetch = async () => {
    throw Error("Discogs unavailable");
  };
  await assert.rejects(
    verifyDiscogs([seed], { userAgent: "test" }),
    /Discogs unavailable/,
  );
  console.log(
    "PASS: live-derived CTDB/Discogs fixture, TOC conversion, isolated error, missing timings, XML rejection, per-track artists, album-artist fallback and PS2 reply.",
  );
} finally {
  globalThis.fetch = original;
  await rm(directory, { recursive: true, force: true });
}
