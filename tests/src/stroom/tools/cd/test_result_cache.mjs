import assert from "node:assert/strict";
import {
  mkdtemp,
  open,
  readFile,
  writeFile,
  appendFile,
  rm,
} from "node:fs/promises";
import {
  ResultCache,
  RESULT_CACHE_BYTES,
  RETAINED_REPORT_BYTES,
  REPORT_BYTES,
  MAX_CANDIDATES,
  MAX_MEDIA,
  checkCandidates,
  compactReport,
  reportText,
} from "../../../../../tools/cd/recognition/report.mjs";
import {
  cachedReport,
  saveReport,
  unsuccessfulDeadline,
} from "../../../../../tools/cd/recognition/cache.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";

assert.equal(RESULT_CACHE_BYTES, 64 * 1024 * 1024);
assert.equal(RETAINED_REPORT_BYTES, 2 * 1024 * 1024);
assert.equal(REPORT_BYTES, 16 * 1024 * 1024);
const toc = { first: 1, offsets: [150], leadout: 15000 };
const id = discId(toc);
const candidate = {
  id: "release",
  title: "Album",
  artist: "Artist",
  media: [{ position: 1, tracks: [{ title: "Track" }] }],
  coverRejectedUrls: ["https://example.test/missing"],
  coverRetryAt: 1234567,
};
const report = {
  discId: id,
  toc,
  selected: { releaseId: "release", mediumPosition: 1 },
  candidates: [candidate],
  artworkRetryable: true,
  artworkRetryAt: 1234567,
  raw: { diagnostics: "x".repeat(1024 * 1024) },
  tracks: ["unused diagnostic"],
  gnudb: { candidates: [candidate], errors: [] },
  ctdb: { raw: "<ctdb>diagnostics</ctdb>" },
  discogs: { raw: ["diagnostics"], errors: [] },
  coverFallback: {
    raw: ["diagnostics"],
    candidates: [candidate],
    errors: [{ message: "retry", retryable: true, retryAt: 1234567 }],
  },
};
const compact = compactReport(report);
assert.equal(compact.report.raw, undefined);
assert.equal(compact.report.ctdb, undefined);
assert.equal(compact.report.discogs.raw, undefined);
assert.equal(compact.report.gnudb.candidates, undefined);
assert.equal(compact.report.coverFallback.raw, undefined);
assert.deepEqual(compact.report.candidates[0], candidate);
assert.equal(compact.report.artworkRetryAt, report.artworkRetryAt);
assert.deepEqual(
  compact.report.coverFallback.errors,
  report.coverFallback.errors,
);
assert.ok(compact.bytes < 2000);
assert.equal(compact.bytes, Buffer.byteLength(JSON.stringify(compact.report)));
compact.report.candidates[0].media[0].tracks[0].title = "Changed";
assert.equal(report.candidates[0].media[0].tracks[0].title, "Track");
for (const outcome of [
  {
    selected: null,
    candidates: [],
    lookupStatus: "no-match",
    lookupRetryAt: Date.now() + 60000,
  },
  {
    error: "Temporary failure",
    lookupStatus: "error",
    lookupRetryAt: Date.now() + 60000,
  },
])
  assert.equal(
    unsuccessfulDeadline(compactReport(outcome).report),
    outcome.lookupRetryAt,
  );

const item = (id) => ({ id, ...compactReport({ ...report, discId: id }) });
const entries = ["a", "b", "c", "d", "e"].map((id) => ({ id }));
const entry = item("a");
const evicted = [];
const cache = new ResultCache({
  maxBytes: entry.bytes * 2,
  onEvict: (id) => evicted.push(id),
});
for (const job of entries.slice(0, 2))
  assert.equal(cache.store(job, item(job.id)), true);
assert.equal(cache.snapshot().bytes, cache.maxBytes);
cache.touch("a");
assert.equal(cache.store(entries[2], item("c")), true);
assert.deepEqual(evicted, ["b"]);
entries[0].working = true;
assert.equal(cache.store(entries[3], item("d")), true);
assert.deepEqual(evicted, ["b", "c"]);
entries[3].pending = true;
assert.equal(
  cache.store(entries[4], item("e")),
  false,
  "Protect active/queued jobs without exceeding the byte allowance",
);
assert.equal(cache.snapshot().bytes, cache.maxBytes);
assert.equal(
  cache.store(entries[0], item("a")),
  true,
  "Replacing a working result does not double-count it",
);
cache.delete("d");
cache.delete("d");
assert.equal(cache.snapshot().bytes, entry.bytes);
const replacement = { id: "a" };
assert.equal(cache.store(replacement, item("a")), true);
assert.equal(cache.snapshot().bytes, entry.bytes);
assert.equal(cache.snapshot().reports, 1);
assert.equal(
  new ResultCache({ maxBytes: 1 }).store(entries[4], item("e")),
  false,
);
assert.throws(
  () => compactReport({ ...report, note: "x".repeat(RETAINED_REPORT_BYTES) }),
  /exceeds/,
);
checkCandidates(Array.from({ length: MAX_CANDIDATES }, () => candidate));
assert.throws(
  () => checkCandidates(Array(MAX_CANDIDATES + 1).fill(candidate)),
  /candidates/,
);
checkCandidates([
  { media: Array(MAX_MEDIA).fill({ tracks: Array(99).fill({}) }) },
]);
assert.throws(
  () => checkCandidates([{ media: Array(MAX_MEDIA + 1).fill({ tracks: [] }) }]),
  /media/,
);
assert.throws(
  () => checkCandidates([{ media: [{ tracks: Array(100).fill({}) }] }]),
  /tracks/,
);
assert.equal(Buffer.byteLength(reportText({ text: "é".repeat(10) }, 31)), 31);
assert.throws(() => reportText({ text: "é".repeat(10) }, 30), /exceeds/);

const directory = await mkdtemp("/tmp/stroom-result-cache-");
const path = directory + "/" + id + ".json";
try {
  await saveReport(directory, id, report);
  const saved = await readFile(path, "utf8");
  assert.equal(
    (await cachedReport(directory, id)).raw.diagnostics.length,
    1024 * 1024,
  );
  assert.equal(JSON.parse(saved).coverFallback.raw[0], "diagnostics");
  await assert.rejects(
    saveReport(directory, id, {
      ...report,
      raw: { padding: "x".repeat(REPORT_BYTES) },
    }),
    /exceeds/,
  );
  assert.equal(
    await readFile(path, "utf8"),
    saved,
    "Rejected reports must not replace the last good disk copy",
  );
  const file = await open(path, "w+");
  await file.truncate(REPORT_BYTES + 1);
  const prototype = Object.getPrototypeOf(file);
  const originalRead = prototype.read;
  let reads = 0;
  prototype.read = async function (...args) {
    ++reads;
    return originalRead.apply(this, args);
  };
  try {
    assert.equal(await cachedReport(directory, id), null);
    assert.equal(
      reads,
      0,
      "Reject oversized files before reading or parsing their contents",
    );
  } finally {
    prototype.read = originalRead;
    await file.close();
  }
  await writeFile(path, saved);
  let grew = false,
    bytesRead = 0;
  prototype.read = async function (...args) {
    if (!grew) {
      grew = true;
      await appendFile(path, " ".repeat(4096));
    }
    const result = await originalRead.apply(this, args);
    bytesRead += result.bytesRead;
    return result;
  };
  try {
    assert.equal(await cachedReport(directory, id), null);
    assert.ok(
      bytesRead <= Buffer.byteLength(saved) + 1,
      "Growth during a read cannot bypass its allocation bound",
    );
  } finally {
    prototype.read = originalRead;
  }
  await writeFile(
    path,
    JSON.stringify({
      ...report,
      cacheVersion: 2,
      candidates: Array(MAX_CANDIDATES + 1).fill(candidate),
    }),
  );
  assert.equal(await cachedReport(directory, id), null);
  console.log(
    "PASS: 64 MiB accounting, LRU/protected entries, compact retry state, report/candidate limits, full disk diagnostics and bounded file reads.",
  );
} finally {
  await rm(directory, { recursive: true, force: true });
}
