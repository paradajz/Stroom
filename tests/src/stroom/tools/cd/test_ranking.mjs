import assert from "node:assert/strict";
import { rankCandidates } from "../../../../../tools/cd/ranking.mjs";

const toc = { offsets: [150, 900], leadout: 2400 }; // 10s, 20s
const medium = (position, lengths, exactDisc = false) => ({
  position,
  exactDisc,
  tracks: lengths.map((milliseconds) => ({ milliseconds })),
});
const candidate = (id, ...media) => ({ id, media });
const close = candidate("close", medium(1, [11000, 22000]));
const best = candidate("best", medium(1, [50000]), medium(2, [10020, 20040]));
const missing = candidate("missing", medium(1, [10000, null]));
const mismatch = candidate("mismatch", medium(1, [10000, 20000, 30000]));
let list = [missing, mismatch, close, best];
let selected = rankCandidates(toc, list);
assert.equal(selected.releaseId, "best");
assert.equal(selected.mediumPosition, 2);
assert.equal(selected.meanDifferenceMs, 30);
assert.equal(selected.maxDifferenceMs, 40);
assert.deepEqual(
  list.map((c) => c.id),
  ["best", "close", "missing", "mismatch"],
);
assert.equal(rankCandidates(toc, [missing, mismatch]), null);
assert.equal(rankCandidates(toc, []), null);
assert.equal(rankCandidates(toc, [candidate("empty")]), null);
const tied = [
  candidate("b", medium(1, [10020, 20040])),
  candidate("a", medium(1, [10020, 20040])),
];
assert.equal(rankCandidates(toc, tied).releaseId, "a");
assert.equal(rankCandidates(toc, tied.reverse()).tiedCandidates, 2);
const exact = candidate("exact", medium(1, [11000, 22000], true));
assert.equal(rankCandidates(toc, [best, exact]).releaseId, "exact");
const spread = candidate("spread", medium(1, [10000, 20200]));
const even = candidate("even", medium(1, [10100, 20100]));
assert.equal(rankCandidates(toc, [spread, even]).releaseId, "even");
// Reported failure: 18 tracks, ~20.159s mean error and 44.720s worst error.
// Matching track count alone must never select this unrelated album.
const longToc = {
  offsets: Array.from({ length: 18 }, (_, i) => 150 + i * 15000),
  leadout: 270150,
};
const unrelated = candidate(
  "wrong-album",
  medium(
    1,
    Array.from(
      { length: 18 },
      (_, i) => 200000 + (i ? (20159 * 18 - 44720) / 17 : 44720),
    ),
  ),
);
assert.equal(rankCandidates(longToc, [unrelated]), null);
assert.match(unrelated.comparison.rejection, /duration/);
assert.ok(Math.abs(unrelated.comparison.meanDifferenceMs - 20159) < 0.001);
assert.equal(unrelated.comparison.maxDifferenceMs, 44720);
// Check both independent limits, inclusive boundaries, and exact-ID precedence.
assert.equal(
  rankCandidates(toc, [candidate("boundary", medium(1, [12000, 22000]))])
    .releaseId,
  "boundary",
);
assert.equal(
  rankCandidates(toc, [candidate("mean-too-high", medium(1, [12001, 22001]))]),
  null,
);
const outlier = candidate(
  "outlier",
  medium(
    1,
    Array.from({ length: 18 }, (_, i) => 200000 + (i ? 0 : 5001)),
  ),
);
assert.equal(rankCandidates(longToc, [outlier]), null);
outlier.media[0].tracks[0].milliseconds = 205000;
assert.equal(rankCandidates(longToc, [outlier]).releaseId, "outlier");
assert.equal(
  rankCandidates(toc, [
    candidate("exact-missing", medium(1, [null, null], true)),
  ]).releaseId,
  "exact-missing",
);
assert.equal(
  rankCandidates(toc, [
    candidate("multi", medium(1, [10000, 26000]), medium(2, [11900, 21900])),
  ]).mediumPosition,
  2,
);
console.log(
  "PASS: timing ranking, track counts, missing durations, multi-disc releases, exact matches, ties and no candidates.",
);
