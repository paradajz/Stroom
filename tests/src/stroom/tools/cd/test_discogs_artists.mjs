import assert from "node:assert/strict";
import { mkdtemp, rm } from "node:fs/promises";
import { verifyDiscogs } from "../../../../../tools/cd/discogs.mjs";
import { lookupDisc } from "../../../../../tools/cd/musicbrainz.mjs";
import { fallbackCover } from "../../../../../tools/cd/cover.mjs";
import { coverInput } from "./cover_fixture.mjs";

const release = {
  id: 123,
  title: "Joint album",
  artists: [{ name: "Artist A (2)", join: "&" }, { name: "Artist B" }],
  formats: [{ name: "CD" }],
  images: [{ type: "primary", uri150: "https://i.discogs.com/cover.jpg" }],
  tracklist: ["One", "Two"].map((title, index) => ({
    title,
    position: String(index + 1),
    type_: "track",
    duration: "1:00",
  })),
};
function cached(seed, candidate = release) {
  return [
    { search: seed, response: { results: [{ id: candidate.id }] } },
    { release: candidate },
  ];
}
const options = { userAgent: "stroom-test/1" };
const originalFetch = globalThis.fetch;
const directory = await mkdtemp("/tmp/stroom-discogs-artists-");
try {
  globalThis.fetch = async () => {
    throw Error("Matching must use cached provider data");
  };
  for (const [credit, names, accepted] of [
    ["Artist A & Artist B", ["Artist A", "Artist B"], true],
    ["Different credited spelling", ["Artist B", "Ártist A"], true],
    ["Artist A & Artist B", ["Artist A"], false],
    ["Artist A & Artist B", ["Artist A", "Unrelated"], false],
    ["Artist A & Artist B", ["Artist A", "Artist B", "Extra"], false],
    ["Artist A & Artist B", undefined, true],
    ["Artist A, Artist B", undefined, true],
    ["Artist A", undefined, false],
  ]) {
    const seed = {
      source: "musicbrainz",
      id: "mb-release",
      title: release.title,
      artist: credit,
      artists: names,
      tracks: ["One", "Two"],
    };
    const result = await verifyDiscogs([seed], options, cached(seed));
    assert.equal(
      result.candidates.length,
      accepted ? 1 : 0,
      JSON.stringify(seed),
    );
    if (accepted) {
      assert.deepEqual(result.candidates[0].artists, ["Artist A", "Artist B"]);
      assert.equal(result.candidates[0].artist, "Artist A, Artist B");
    }
  }
  // Prefer full-size images before conversion; retain smaller URL fallbacks.
  const full = "https://i.discogs.com/full.jpg";
  const thumbnail = "https://i.discogs.com/thumbnail.jpg";
  const thumb = "https://i.discogs.com/release-thumb.jpg";
  const imageSeed = {
    artist: "Artist A & Artist B",
    title: release.title,
    tracks: ["One", "Two"],
  };
  for (const [images, expected] of [
    [[{ type: "primary", uri: full, uri150: thumbnail }], full],
    [[{ type: "primary", uri150: thumbnail }], thumbnail],
    [
      [
        {
          type: "primary",
          uri: "https://unrelated.test/image.jpg",
          uri150: thumbnail,
        },
      ],
      thumbnail,
    ],
    [[], thumb],
    [[{ uri: thumbnail }, { type: "primary", uri: full }], full],
  ]) {
    const result = await verifyDiscogs(
      [imageSeed],
      options,
      cached(imageSeed, {
        ...release,
        images,
        thumb,
      }),
    );
    assert.equal(result.candidates.length, 1);
    assert.equal(result.candidates[0].coverUrl, expected);
  }

  // Compilation aliases are equivalent, not a wildcard for any artist.
  for (const [credit, provider] of [
    ["Various Artists", "Various"],
    ["Various", "Various Artists"],
  ]) {
    for (const structured of [true, false]) {
      const seed = {
        title: release.title,
        artist: credit,
        ...(structured ? { artists: [credit] } : {}),
        tracks: ["One", "Two"],
      };
      const compilation = { ...release, artists: [{ name: provider }] };
      const result = await verifyDiscogs(
        [seed],
        options,
        cached(seed, compilation),
      );
      assert.equal(result.candidates.length, 1);
      assert.equal(
        result.candidates[0].artist,
        provider,
        "Keep the display credit unchanged",
      );
      assert.deepEqual(
        result.candidates[0].media[0].tracks.map((t) => t.milliseconds),
        [60000, 60000],
      );
      for (const change of ["artist", "title", "tracks", "count"]) {
        const different = structuredClone(compilation);
        if (change === "artist")
          different.artists = [{ name: "Unrelated artist" }];
        if (change === "title") different.title = "Different album";
        if (change === "tracks")
          different.tracklist[0].title = "Different song";
        if (change === "count") different.tracklist.pop();
        assert.equal(
          (await verifyDiscogs([seed], options, cached(seed, different)))
            .candidates.length,
          0,
          change,
        );
      }
    }
  }

  for (const name of ["AC/DC", "Earth, Wind & Fire"]) {
    const seed = {
      title: release.title,
      artist: name,
      artists: [name],
      tracks: ["One", "Two"],
    };
    const single = { ...release, artists: [{ name }] };
    assert.equal(
      (await verifyDiscogs([seed], options, cached(seed, single))).candidates
        .length,
      1,
    );
    const split = {
      ...release,
      artists: name
        .split(/[,&/]/)
        .map((name) => ({ name: name.trim() }))
        .filter((a) => a.name),
    };
    assert.equal(
      (await verifyDiscogs([seed], options, cached(seed, split))).candidates
        .length,
      0,
    );
  }

  for (const name of ["坂本龍一", "Кино"]) {
    const seed = {
      title: release.title,
      artist: name,
      artists: [name],
      tracks: ["One", "Two"],
    };
    const candidate = { ...release, artists: [{ name }] };
    assert.equal(
      (await verifyDiscogs([seed], options, cached(seed, candidate))).candidates
        .length,
      1,
    );
    delete seed.artists;
    assert.equal(
      (await verifyDiscogs([seed], options, cached(seed, candidate))).candidates
        .length,
      1,
    );
    candidate.artists = [{ name: "別の名前" }];
    assert.equal(
      (await verifyDiscogs([seed], options, cached(seed, candidate))).candidates
        .length,
      0,
    );
  }

  // Exercise the MusicBrainz -> cover fallback path, preserving canonical names
  // separately from the combined display credit and using fetched Discogs data.
  const toc = { first: 1, offsets: [150, 4650], leadout: 9150 };
  globalThis.fetch = async () =>
    Response.json({
      releases: [
        {
          id: "12345678-1234-1234-1234-123456789abc",
          title: release.title,
          "artist-credit": [
            {
              name: "Credited A",
              artist: { name: "Artist A" },
              joinphrase: " & ",
            },
            { name: "Artist B" },
          ],
          media: [
            {
              position: 1,
              tracks: release.tracklist.map((track) => ({
                number: track.position,
                title: track.title,
                length: 60000,
              })),
            },
          ],
        },
      ],
    });
  const report = await lookupDisc(toc, options);
  const winner = report.candidates[0];
  assert.deepEqual(winner.artists, ["Artist A", "Artist B"]);
  assert.equal(winner.artist, "Credited A & Artist B");
  const selected = structuredClone(report.selected);
  report.discogs = {
    raw: cached({ artist: winner.artist, title: winner.title }),
  };
  globalThis.fetch = async (url) => {
    assert.equal(String(url), "https://i.discogs.com/cover.jpg");
    return new Response(coverInput, {
      headers: { "content-type": "image/png" },
    });
  };
  await fallbackCover(report, { ...options, directory });
  assert.ok(winner.coverFile, "Structured credits must reach the cover search");
  assert.equal(winner.artist, "Credited A & Artist B");
  assert.deepEqual(report.selected, selected);
  assert.deepEqual(report.coverFallback.candidates[0].artists, [
    "Artist A",
    "Artist B",
  ]);
} finally {
  globalThis.fetch = originalFetch;
  await rm(directory, { recursive: true, force: true });
}
console.log(
  "PASS: complete multi-artist credits, legacy display credits, intact band names and MusicBrainz cover fallback.",
);
