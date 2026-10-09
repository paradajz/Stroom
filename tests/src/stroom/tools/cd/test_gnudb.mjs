import assert from "node:assert/strict";
import {
  gnudbHello,
  lookupGnudb,
} from "../../../../../tools/cd/recognition/gnudb.mjs";
import { selectDisc } from "../../../../../tools/cd/recognition/lookup.mjs";
import { rankCandidates } from "../../../../../tools/cd/recognition/ranking.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { compactReport } from "../../../../../tools/cd/recognition/report.mjs";
import {
  cachedReport,
  saveReport,
} from "../../../../../tools/cd/recognition/cache.mjs";
import { resolveCover } from "../../../../../tools/cd/recognition/cover.mjs";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { coverInput, assertCover } from "./cover_fixture.mjs";

// Every provider request is mocked. Never send test identities to the real service.
const original = globalThis.fetch;
const originalNow = Object.getOwnPropertyDescriptor(performance, "now");
let clock = 0;
Object.defineProperty(performance, "now", {
  configurable: true,
  value: () => (clock += 2000),
});
const toc = { first: 1, offsets: [150, 15150], leadout: 30150 };
const options = {
  contact: "developer+stroom@example.test",
  userAgent: "stroom-test",
};
const xmcd = (offset = 15150) => `210 data abcdef01 entry follows
# xmcd
# Track frame offsets:
# 150
# ${offset}
#
# Disc length: 402 seconds
DTITLE=Various / Al
DTITLE=bum
DYEAR=2000
TTITLE0=Ivana Brkić / First\\nSong
TTITLE1=Colonia / Second
.
`;
let requests = [];
let responses = [];
globalThis.fetch = async (value, init) => {
  const url = new URL(value);
  requests.push(url);
  assert.equal(url.origin, "https://gnudb.gnudb.org");
  assert.equal(url.pathname, "/~cddb/cddb.cgi");
  assert.equal(
    url.searchParams.get("hello"),
    "developer+stroom example.test PS2Vis-CD-Recognition v0.1",
  );
  assert.equal(url.searchParams.get("proto"), "6");
  assert.equal(init.headers["User-Agent"], options.userAgent);
  assert.ok(responses.length, "Unexpected extra request");
  const response = responses.shift();
  return response instanceof Response ? response : new Response(response);
};
try {
  for (const contact of [
    undefined,
    "",
    "https://example.test",
    "root@localhost",
    "a@b.com\nattack",
    "user name@host.com",
  ])
    assert.equal(gnudbHello(contact), null);
  await lookupGnudb(toc, { ...options, contact: "https://example.test" });
  assert.equal(requests.length, 0);
  responses = ["200 data abcdef01 Various / Album\n", xmcd()];
  let result = await lookupGnudb(toc, options);
  assert.equal(
    requests[0].searchParams.get("cmd"),
    "cddb query 06019002 2 150 15150 402",
  );
  assert.equal(requests[1].searchParams.get("cmd"), "cddb read data abcdef01");
  assert.equal(result.candidates[0].title, "Album");
  assert.equal(result.candidates[0].media[0].tracks[0].artist, "Ivana Brkić");
  assert.equal(result.candidates[0].media[0].tracks[0].title, "First\nSong");
  assert.equal(result.candidates[0].media[0].tracks[1].milliseconds, 200000);
  assert.ok(rankCandidates(toc, result.candidates));

  // Even a server-labelled exact match must pass independent stored timings.
  responses = ["200 data abcdef01 Wrong album\n", xmcd(20150)];
  result = await lookupGnudb(toc, options);
  assert.equal(rankCandidates(toc, result.candidates), null);
  responses = ["202 No match\n"];
  assert.equal((await lookupGnudb(toc, options)).candidates.length, 0);
  // Exercise the service fallback before the provider refusal tests.
  const gnudbFetch = globalThis.fetch;
  // MusicBrainz remains primary, even when GnuDB is configured.
  globalThis.fetch = async (url) => {
    assert.equal(new URL(url).hostname, "musicbrainz.org");
    return Response.json({
      releases: [
        {
          id: "mb",
          title: "Primary",
          "artist-credit": [{ name: "Artist" }],
          media: [
            {
              position: 1,
              discs: [{ id: discId(toc) }],
              tracks: [{ title: "A" }, { title: "B" }],
            },
          ],
        },
      ],
    });
  };
  assert.equal((await selectDisc(toc, options)).selected.releaseId, "mb");

  let hosts = [];
  let mbFails = false;
  globalThis.fetch = async (url, init) => {
    const host = new URL(url).hostname;
    hosts.push(host);
    if (host === "musicbrainz.org") {
      if (mbFails) throw Error("MusicBrainz unavailable");
      return Response.json({ releases: [] });
    }
    if (host === "gnudb.gnudb.org") return gnudbFetch(url, init);
    assert.equal(host, "db.cue.tools");
    return new Response("<ctdb></ctdb>");
  };
  responses = ["200 data abcdef01 Various / Album\n", xmcd()];
  const matched = await selectDisc(toc, options);
  assert.equal(matched.match, "gnudb");
  assert.equal(matched.selected.releaseId, "gnudb-data-abcdef01");
  assert.equal(matched.candidateCount, 1);
  assert.equal(matched.gnudb.candidates[0].source, "gnudb");
  assert.ok(!hosts.includes("db.cue.tools"));

  mbFails = true;
  responses = ["200 data abcdef01 Various / Album\n", xmcd()];
  assert.equal((await selectDisc(toc, options)).match, "gnudb");

  // Reported Justified failure: an explicitly incorrect artist has the lower
  // ID and ties the correct entry on timings. A third entry confirms the credit.
  const justified = JSON.parse(
    await readFile(new URL("./fixtures/justified-gnudb.json", import.meta.url)),
  );
  assert.equal(discId(justified.toc), justified.discId);
  responses = [
    "211 matches\n" +
      justified.entries
        .map((entry) => `data ${entry.id} ${entry.credit}\n`)
        .join("") +
      ".\n",
    ...justified.entries.map((entry) =>
      [
        "210 entry follows",
        "# Track frame offsets:",
        ...justified.toc.offsets.map((offset) => `# ${offset}`),
        "#",
        `# Disc length: ${entry.seconds} seconds`,
        `DTITLE=${entry.credit}`,
        ...justified.tracks.map(
          (title, i) =>
            `TTITLE${i}=${entry.numberedTitles ? String(i + 1).padStart(2, "0") : ""}${i === 0 ? (entry.firstTrack ?? title) : title}`,
        ),
        ".\n",
      ].join("\n"),
    ),
  ];
  const credited = await selectDisc(justified.toc, options);
  assert.equal(credited.selected.releaseId, "gnudb-data-b50ed887");
  assert.equal(credited.selected.tiedCandidates, 2);
  assert.equal(credited.selected.maxDifferenceMs, 280);
  assert.equal(credited.selected.corroboratingGnudbEntries, 1);
  assert.equal(credited.candidates[0].artist, "Justin Timberlake");
  assert.equal(credited.candidates[0].albumCreditExplicit, true);
  assert.equal(credited.candidates[1].albumCreditExplicit, true);
  assert.equal(credited.candidates[0].media[0].tracks[0].title, "Senorita");
  // Compaction must preserve the evidence used to break the tie.
  const compact = compactReport(credited).report;
  assert.equal(
    rankCandidates(justified.toc, compact.candidates.reverse()).releaseId,
    "gnudb-data-b50ed887",
  );
  // Even the suspicious self-titled entry remains usable on its own.
  assert.equal(
    rankCandidates(justified.toc, [credited.candidates[1]]).releaseId,
    "gnudb-data-b50ed885",
  );

  const directory = await mkdtemp("/tmp/stroom-justified-cover-");
  const providerFetch = globalThis.fetch;
  try {
    // The deployed cache is version 2 and permanently records no artwork for
    // the wrong credit. An upgrade must reconsider it without a manual purge.
    const stale = structuredClone(credited);
    stale.selected.releaseId = "gnudb-data-b50ed885";
    stale.artworkRetryable = false;
    stale.artworkRetryAt = 0;
    await saveReport(directory, justified.discId, stale);
    assert.equal(await cachedReport(directory, justified.discId), null);
    await saveReport(directory, justified.discId, credited);
    assert.equal(
      (await cachedReport(directory, justified.discId)).selected.releaseId,
      "gnudb-data-b50ed887",
      "An unchanged GnuDB selection remains reusable offline",
    );
    globalThis.fetch = async (value) => {
      const url = new URL(value);
      if (url.pathname === "/database/search") {
        assert.equal(url.hostname, "api.discogs.com");
        assert.equal(url.searchParams.get("artist"), "Justin Timberlake");
        assert.equal(url.searchParams.get("release_title"), "Justified");
        return Response.json({ results: [{ id: 123 }] });
      }
      if (url.pathname === "/releases/123") {
        assert.equal(url.hostname, "api.discogs.com");
        return Response.json({
          title: "Justified",
          artists: [{ name: "Justin Timberlake" }],
          formats: [{ name: "CD" }],
          images: [
            { type: "primary", uri: "https://i.discogs.com/justified.jpg" },
          ],
          tracklist: justified.tracks.map((title, i) => ({
            type_: "track",
            title,
            position: String(i + 1),
            duration: "",
          })),
        });
      }
      assert.equal(url.href, "https://i.discogs.com/justified.jpg");
      return new Response(coverInput, {
        headers: { "content-type": "image/png" },
      });
    };
    await resolveCover(credited, { ...options, directory });
    assert.equal(credited.artworkRetryable, false);
    assert.equal(credited.candidates[0].coverSource.releaseId, "discogs-123");
    assertCover(
      await readFile(directory + "/" + credited.candidates[0].coverFile),
    );
    await saveReport(directory, justified.discId, credited);
    assert.equal(
      (await cachedReport(directory, justified.discId)).candidates[0].coverFile,
      credited.candidates[0].coverFile,
      "The corrected selection and artwork survive cache reloads",
    );
  } finally {
    globalThis.fetch = providerFetch;
    await rm(directory, { recursive: true, force: true });
  }

  mbFails = false;
  for (const replies of [
    ["202 No match\n"],
    ["200 data abcdef01 Wrong album\n", xmcd(20150)],
  ]) {
    hosts = [];
    responses = replies;
    const unmatched = await selectDisc(toc, options);
    assert.equal(unmatched.selected, null);
    assert.ok(hosts.includes("db.cue.tools"));
  }
  hosts = [];
  const skipped = await selectDisc(toc, {
    ...options,
    contact: "https://example.test",
  });
  assert.match(skipped.gnudb.skipped, /real email/);
  assert.ok(!hosts.includes("gnudb.gnudb.org"));
  assert.ok(hosts.includes("db.cue.tools"));

  responses = [new Response(null, { status: 502 })];
  hosts = [];
  await assert.rejects(selectDisc(toc, options), /gnudb: HTTP 502/);
  assert.ok(hosts.includes("db.cue.tools"));
  globalThis.fetch = gnudbFetch;

  responses = [
    "211 inexact matches\ndata abcdef01 Album\ndata abcdef01 Duplicate\nrock abcdef02 Album\n.\n",
    xmcd(),
    "500 Access refused\n",
  ];
  result = await lookupGnudb(toc, options);
  assert.equal(result.candidates.length, 1);
  assert.equal(result.errors.length, 1);
  const before = requests.length;
  await assert.rejects(
    lookupGnudb(toc, options),
    /disabled until service restart/,
  );
  assert.equal(requests.length, before);

  // Fresh provider instance for isolated response-format and read-limit checks.
  const fresh =
    await import("../../../../../tools/cd/recognition/gnudb.mjs?fresh");
  responses = ["210 matches\ndata abcdef01 Album\n"];
  await assert.rejects(fresh.lookupGnudb(toc, options), /Incomplete/);
  responses = [
    "210 matches\ndata abcdef01 A\ndata abcdef02 B\ndata abcdef03 C\ndata abcdef04 D\n.\n",
    xmcd(),
    xmcd(),
    xmcd(),
  ];
  const start = requests.length;
  assert.equal((await fresh.lookupGnudb(toc, options)).candidates.length, 3);
  assert.equal(requests.length - start, 4);
  responses = ["200 data abcdef01 Album\n", xmcd().replace("# 15150\n", "")];
  assert.equal((await fresh.lookupGnudb(toc, options)).candidates.length, 0);

  responses = [new Response(null, { status: 403 })];
  await assert.rejects(fresh.lookupGnudb(toc, options), /HTTP 403/);
  const refusedCount = requests.length;
  await assert.rejects(
    fresh.lookupGnudb(toc, options),
    /disabled until service restart/,
  );
  assert.equal(requests.length, refusedCount);
  const limited =
    await import("../../../../../tools/cd/recognition/gnudb.mjs?limited");
  responses = [
    new Response(null, { status: 429, headers: { "Retry-After": "3600" } }),
  ];
  await assert.rejects(limited.lookupGnudb(toc, options), /HTTP 429/);
  const limitedCount = requests.length;
  await assert.rejects(
    limited.lookupGnudb(toc, options),
    /Provider retry deferred/,
  );
  assert.equal(requests.length, limitedCount);
} finally {
  globalThis.fetch = original;
  if (originalNow) Object.defineProperty(performance, "now", originalNow);
  else delete performance.now;
}
console.log(
  "PASS: GnuDB identity, query-before-read, returned IDs, UTF-8 credits, timings, bounded reads, refusals and primary-provider ordering",
);
