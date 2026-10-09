import assert from "node:assert/strict";
import { gnudbHello, lookupGnudb } from "../../../../../tools/cd/gnudb.mjs";
import { selectDisc } from "../../../../../tools/cd/lookup.mjs";
import { rankCandidates } from "../../../../../tools/cd/ranking.mjs";
import { discId } from "../../../../../tools/cd/disc.mjs";

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
  const fresh = await import("../../../../../tools/cd/gnudb.mjs?fresh");
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
  const limited = await import("../../../../../tools/cd/gnudb.mjs?limited");
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
