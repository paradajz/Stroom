import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { verifyDiscogs } from "../../../../../tools/cd/discogs.mjs";
import { resolveCover } from "../../../../../tools/cd/cover.mjs";
import { rankCandidates } from "../../../../../tools/cd/ranking.mjs";
import { discId } from "../../../../../tools/cd/disc.mjs";
import { metadataReply } from "../../../../../tools/cd/response.mjs";
import { cd } from "../../../../../tools/contracts/load.mjs";
import { coverInput, assertCover } from "./cover_fixture.mjs";

// Discogs 25442686 has 14 main-disc tracks plus these four bonus-disc tracks.
// The matched MusicBrainz edition has 13 on disc 1; only disc 2 is in the PS2.
const titles = [
  "The Gypsy Maid (club remix)",
  "Prelude in C (club remix)",
  "Requiem (Breaks remix)",
  "Child in Paradise (Co-Fusion mix)",
];
const seed = {
  artist: "Maksim",
  title: "Electrik",
  source: "musicbrainz",
  id: "30411209-eadc-4a80-b73e-414498a3d90a",
  tracks: titles,
};
const coverUrl = "https://i.discogs.com/multidisc-fixture.jpg";
const release = {
  id: 25442686,
  title: "Electrik",
  artists: [{ name: "Maksim" }],
  formats: [{ name: "CD", qty: "2" }],
  images: [{ type: "secondary", uri150: coverUrl }],
  tracklist: [
    ...Array.from({ length: 14 }, (_, i) => ({
      type_: "track",
      position: `1-${String(i + 1).padStart(2, "0")}`,
      title: `Main disc track ${i + 1}`,
      duration: "3:00",
    })),
    { type_: "heading", position: "", title: "Bonus CD - The Remixes" },
    ...titles.map((title, i) => ({
      type_: "track",
      position: `2-0${i + 1}`,
      title: title.toUpperCase(),
      duration: "3:00",
    })),
  ],
};
const options = { userAgent: "test" };
const cached = (value) => [
  { search: seed, response: { results: [{ id: value.id }] } },
  { release: value },
];
const verify = (value) => verifyDiscogs([seed], options, cached(value));
const toc = { first: 1, offsets: [150, 13650, 27150, 40650], leadout: 54150 };
const originalFetch = globalThis.fetch;
const directory = await mkdtemp("/tmp/stroom-discogs-multidisc-");
const requests = [];
globalThis.fetch = async (url) => {
  requests.push(url);
  assert.equal(
    url,
    coverUrl,
    "Cached provider records must not trigger another search",
  );
  return new Response(coverInput, {
    headers: { "content-type": "image/jpeg" },
  });
};
try {
  const result = await verify(release);
  assert.equal(result.candidates.length, 1);
  const candidate = result.candidates[0];
  assert.equal(candidate.media[0].position, 2);
  assert.equal(candidate.verification.agreeingTrackNames, 4);
  assert.equal(rankCandidates(toc, result.candidates).mediumPosition, 2);
  assert.deepEqual(
    candidate.media[0].tracks.map((track) => track.number),
    ["1", "2", "3", "4"],
  );

  const winner = {
    ...seed,
    coverAvailable: false,
    media: [
      {
        position: 1,
        tracks: release.tracklist
          .slice(0, 13)
          .map((track) => ({ title: track.title })),
      },
      {
        position: 2,
        tracks: titles.map((title) => ({ title, milliseconds: 180000 })),
      },
    ],
  };
  delete winner.tracks;
  const coverRelease = structuredClone(release);
  for (const track of coverRelease.tracklist.slice(-4)) delete track.duration;
  const report = {
    toc,
    discId: discId(toc),
    selected: { releaseId: winner.id, mediumPosition: 2 },
    candidates: [winner],
    discogs: { raw: cached(coverRelease) },
  };
  const metadataBefore = structuredClone(winner.media);
  await resolveCover(report, { ...options, directory });
  assert.equal(requests.length, 1);
  assert.ok(
    winner.coverFile,
    "A matching bonus disc must reach release artwork",
  );
  await assertCover(await readFile(`${directory}/${winner.coverFile}`));
  assert.equal(winner.coverSource.matchBasis, "artist-album-all-track-names");
  assert.deepEqual(winner.media, metadataBefore);
  assert.equal(report.selected.mediumPosition, 2);
  const packet = metadataReply(
    report,
    { service: "127.0.0.1", request: 1, track: 4 },
    cd.CD_LOOKUP_PORT,
  );
  const reply = JSON.parse(
    packet.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES),
  ).data;
  assert.equal(reply.title, titles[3]);
  assert.ok(reply.artwork_url);

  const single = structuredClone(release);
  single.tracklist = single.tracklist
    .slice(-4)
    .map((track, i) => ({ ...track, position: String(i + 1) }));
  assert.equal((await verify(single)).candidates[0].id, "discogs-25442686");
  const dotted = structuredClone(release);
  for (const track of dotted.tracklist)
    track.position = track.position.replace("-", ".");
  assert.equal((await verify(dotted)).candidates[0].media[0].position, 2);

  // Same names on two discs must retain separate timing evidence for ranking.
  const two = structuredClone(single);
  two.tracklist = [1, 2].flatMap((disc) =>
    single.tracklist.map((track, i) => ({
      ...track,
      position: `${disc}-${i + 1}`,
      duration: disc === 1 ? "8:00" : "3:00",
    })),
  );
  const both = await verify(two);
  assert.equal(both.candidates.length, 2);
  assert.equal(rankCandidates(toc, both.candidates).mediumPosition, 2);

  for (const change of ["duplicate", "gap", "mixed", "nested", "wrong-title"]) {
    const bad = structuredClone(release);
    if (change === "duplicate") bad.tracklist.at(-1).position = "2-03";
    if (change === "gap") bad.tracklist.at(-1).position = "2-05";
    if (change === "mixed") bad.tracklist.at(-1).position = "4";
    if (change === "nested") bad.tracklist.at(-1).sub_tracks = [];
    if (change === "wrong-title")
      bad.tracklist.at(-1).title = "Unrelated track";
    assert.equal((await verify(bad)).candidates.length, 0, change);
  }
} finally {
  globalThis.fetch = originalFetch;
  await rm(directory, { recursive: true, force: true });
}
console.log(
  "PASS: multi-disc Electrik cover, metadata preservation, independent disc ranking, single-disc compatibility and malformed numbering rejection",
);
