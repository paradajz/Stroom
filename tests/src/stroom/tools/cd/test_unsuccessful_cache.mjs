import assert from "node:assert/strict";
import { mkdtemp, rm } from "node:fs/promises";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import {
  cachedReport,
  saveReport,
} from "../../../../../tools/cd/recognition/cache.mjs";

const directory = await mkdtemp("/tmp/stroom-unsuccessful-cache-");
const toc = { first: 1, offsets: [150, 15363], leadout: 32314 };
const id = discId(toc);
const now = Date.now;
let clock = now();
Date.now = () => clock;
const report = {
  discId: id,
  toc,
  selected: null,
  candidates: [],
  lookupStatus: "no-match",
  lookupRetryAt: clock + 86400000,
};
try {
  await saveReport(directory, id, report);
  assert.equal((await cachedReport(directory, id)).lookupStatus, "no-match");
  clock = report.lookupRetryAt - 1;
  assert.ok(await cachedReport(directory, id));
  clock++;
  assert.equal(await cachedReport(directory, id), null);
  const error = {
    discId: id,
    toc,
    error: "Provider unavailable",
    lookupStatus: "error",
    lookupRetryAt: clock + 60000,
  };
  await saveReport(directory, id, error);
  assert.equal((await cachedReport(directory, id)).error, error.error);
  clock = error.lookupRetryAt;
  assert.equal(await cachedReport(directory, id), null);
  for (const change of [
    { lookupRetryAt: undefined },
    { lookupRetryAt: "forever" },
    { lookupRetryAt: Infinity },
    { lookupRetryAt: -1 },
    { lookupStatus: undefined },
    { lookupStatus: "unknown" },
    { errors: { musicbrainz: "HTTP 503" } },
    { gnudb: { errors: [{ message: "Timeout" }] } },
    { discogs: { errors: [{ message: "Timeout" }] } },
    { error: "Provider unavailable" },
    { candidates: undefined },
    { discId: "wrong" },
    { toc: { ...toc, leadout: toc.leadout + 1 } },
  ]) {
    await saveReport(directory, id, {
      ...report,
      lookupRetryAt: clock + 86400000,
      ...change,
    });
    assert.equal(await cachedReport(directory, id), null);
  }
  console.log(
    "PASS: unexpired no-match and error reports, exact expiry boundaries, unsupported/malformed cache rejection and outage/no-match separation.",
  );
} finally {
  Date.now = now;
  await rm(directory, { recursive: true, force: true });
}
