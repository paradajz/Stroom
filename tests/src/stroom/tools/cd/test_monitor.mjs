import assert from "node:assert/strict";
import {
  Monitor,
  queueSnapshot,
} from "../../../../../tools/cd/recognition/monitor.mjs";

const queues = queueSnapshot(
  [
    { pending: true, queuedAt: 1000 },
    { pending: true, queuedAt: 2000 },
    { pending: true, recognitionComplete: true, queuedAt: 500 },
    { working: true, running: "recognition" },
    { working: true, running: "artwork", recognitionComplete: true },
    { working: true }, // A disk read is not a running provider lookup.
    { pending: true, cachePending: true },
    { report: {}, recognitionComplete: true },
  ],
  10000,
);
assert.deepEqual(queues, {
  recognition: { waiting: 2, running: 1, oldestWaitingMs: 9000 },
  artwork: { waiting: 1, running: 1, oldestWaitingMs: 9500 },
});

let now = 0;
let freeBytes = 8_000_000_000;
let fsError = null;
let filesystemCalls = 0;
const events = [];
const monitor = new Monitor({
  now: () => now,
  wallNow: () => 1800000000000 + now,
  filesystem: async () => {
    ++filesystemCalls;
    if (fsError) throw fsError;
    return { bavail: freeBytes / 4096, bsize: 4096 };
  },
  log: (event) => events.push(event),
});
const snapshot = {
  queues: queueSnapshot([]),
  directory: "/cache",
  cache: {
    bytes: 840_000_000,
    maxBytes: 1_000_000_000,
    targetBytes: 900_000_000,
    discs: 10,
    maxDiscs: 10000,
    targetDiscs: 9000,
    files: 20,
    maxFiles: 20000,
    targetFiles: 18000,
    evictions: 3,
  },
};
const warnings = (name) =>
  events.filter(
    (event) =>
      event.name === name && ["warning", "critical"].includes(event.event),
  );
const recovered = (name) =>
  events.filter((event) => event.name === name && event.event === "recovered");
const summary = () =>
  events.filter((event) => event.event === "summary").at(-1);

monitor.lookup("matched");
monitor.lookup("noMatch");
monitor.lookup("failed");
await monitor.sample(snapshot);
assert.deepEqual(summary().lookupsSinceLastSummary, {
  matched: 1,
  noMatch: 1,
  failed: 1,
});
assert.equal(summary().cache.evictionsSinceLastSummary, 3);
assert.equal(summary().storage.freeBytes, freeBytes);
await monitor.sample(snapshot);
assert.deepEqual(summary().lookupsSinceLastSummary, {
  matched: 0,
  noMatch: 0,
  failed: 0,
});
assert.equal(summary().cache.evictionsSinceLastSummary, 0);
assert.equal(events.filter((event) => event.event !== "summary").length, 0);

snapshot.queues.recognition = {
  waiting: 4,
  running: 1,
  oldestWaitingMs: 60000,
};
await monitor.sample(snapshot);
snapshot.queues.recognition.oldestWaitingMs++;
await monitor.sample(snapshot);
assert.equal(warnings("recognition-queue").length, 0);
now += 60000;
await monitor.sample(snapshot);
assert.equal(warnings("recognition-queue").length, 1);
now += 60000;
await monitor.sample(snapshot);
assert.equal(
  warnings("recognition-queue").length,
  1,
  "Do not repeat warnings every minute",
);
now += 240000;
await monitor.sample(snapshot);
assert.equal(warnings("recognition-queue").length, 2);
snapshot.queues.recognition = queueSnapshot([]).recognition;
await monitor.sample(snapshot);
assert.equal(recovered("recognition-queue").length, 1);
snapshot.queues.artwork.oldestWaitingMs = 180000;
await monitor.sample(snapshot);
assert.equal(warnings("artwork-queue").length, 0);
snapshot.queues.artwork.oldestWaitingMs++;
await monitor.sample(snapshot);
assert.equal(warnings("artwork-queue").length, 1);
snapshot.queues.artwork.oldestWaitingMs = 0;
await monitor.sample(snapshot);
assert.equal(recovered("artwork-queue").length, 1);

for (let i = 0; i < 4; ++i)
  monitor.providerFinished("musicbrainz.org", { failed: true });
monitor.providerFinished("musicbrainz.org", { status: 200 });
const malformed = Error("Malformed response with private payload");
monitor.providerError("musicbrainz.org", malformed);
monitor.providerError("musicbrainz.org", malformed);
monitor.providerDeferred("musicbrainz.org");
monitor.providerCooldown("musicbrainz.org", 1800000000000 + now + 120000);
await monitor.sample(snapshot);
assert.equal(warnings("provider-musicbrainz.org").length, 1);
assert.equal(summary().providers["musicbrainz.org"].protocolErrors, 1);
assert.equal(summary().providers["musicbrainz.org"].requests, 5);
assert.equal(summary().providers["musicbrainz.org"].deferred, 1);
assert.ok(summary().providers["musicbrainz.org"].cooldownUntil);
for (let i = 0; i < 6; ++i)
  monitor.providerFinished("musicbrainz.org", { status: 404 });
await monitor.sample(snapshot);
assert.equal(
  recovered("provider-musicbrainz.org").length,
  1,
  "Recover below 50% failures",
);
assert.equal(summary().providers["musicbrainz.org"].notFound, 6);
const lastSuccess = summary().providers["musicbrainz.org"].lastHttpSuccessAt;
for (let i = 0; i < 12; ++i)
  monitor.providerFinished("musicbrainz.org", { cancelled: true });
await monitor.sample(snapshot);
assert.equal(summary().providers["musicbrainz.org"].cancelled, 12);
assert.equal(summary().providers["musicbrainz.org"].failures, 4);
assert.equal(
  summary().providers["musicbrainz.org"].lastHttpSuccessAt,
  lastSuccess,
);
monitor.providerFinished("musicbrainz.org", { failed: true });
await monitor.sample(snapshot);
assert.equal(
  warnings("provider-musicbrainz.org").length,
  2,
  "Cancelled requests cannot dilute the provider's failure rate",
);
now += 300000;
await monitor.sample(snapshot);
assert.equal(summary().providers["musicbrainz.org"].requests, 0);
assert.equal(summary().providers["musicbrainz.org"].protocolErrors, 0);
assert.equal(summary().providers["musicbrainz.org"].cooldownUntil, null);

snapshot.cache.bytes = 1_000_000_001;
await monitor.sample(snapshot);
now += 299999;
await monitor.sample(snapshot);
assert.equal(warnings("cache-quota").length, 0);
now++;
await monitor.sample(snapshot);
assert.equal(warnings("cache-quota").length, 1);
snapshot.cache.bytes = 900_000_000;
snapshot.cache.evictions++;
await monitor.sample(snapshot);
assert.equal(recovered("cache-quota").length, 1);
assert.equal(summary().cache.evictionsSinceLastSummary, 1);
for (const [field, maximum, target] of [
  ["discs", "maxDiscs", "targetDiscs"],
  ["files", "maxFiles", "targetFiles"],
]) {
  const before = warnings("cache-quota").length;
  snapshot.cache[field] = snapshot.cache[maximum];
  await monitor.sample(snapshot);
  now += 300000;
  await monitor.sample(snapshot);
  assert.equal(
    warnings("cache-quota").length,
    before,
    "The count limit itself is allowed",
  );
  ++snapshot.cache[field];
  await monitor.sample(snapshot);
  now += 300000;
  await monitor.sample(snapshot);
  assert.equal(warnings("cache-quota").length, before + 1);
  assert.equal(warnings("cache-quota").at(-1)[field], snapshot.cache[field]);
  snapshot.cache[field] = snapshot.cache[target];
  await monitor.sample(snapshot);
}

freeBytes = 1_000_000_000;
await monitor.sample(snapshot);
assert.equal(warnings("disk-free").length, 0);
freeBytes--;
await monitor.sample(snapshot);
assert.equal(warnings("disk-free").at(-1).event, "warning");
freeBytes = 249_999_999;
await monitor.sample(snapshot);
assert.equal(warnings("disk-free").at(-1).event, "critical");
freeBytes = 500_000_000;
await monitor.sample(snapshot);
assert.equal(warnings("disk-free").at(-1).event, "warning");
freeBytes = 8_000_000_000;
await monitor.sample(snapshot);
assert.equal(recovered("disk-free").length, 1);

const error = Object.assign(Error("secret filesystem path"), {
  code: "ENOSPC",
});
monitor.storageFailure("report-write", error);
monitor.storageFailure("report-write", error);
assert.equal(
  warnings("storage-report-write").length,
  1,
  "Storage failures warn immediately but do not flood",
);
await monitor.sample(snapshot);
assert.equal(summary().storage.failuresSinceLastSummary, 2);
assert.deepEqual(summary().storage.unresolvedOperations, ["report-write"]);
now += 300000;
await monitor.sample(snapshot);
assert.equal(warnings("storage-report-write").length, 2);
monitor.storageSuccess("cover-write");
assert.equal(recovered("storage-report-write").length, 0);
monitor.storageSuccess("report-write");
assert.equal(recovered("storage-report-write").length, 1);
fsError = Object.assign(Error("disk query failed"), { code: "EIO" });
await monitor.sample(snapshot);
assert.equal(warnings("storage-filesystem").length, 1);
assert.equal(summary().storage.freeBytes, null);
fsError = null;
await monitor.sample(snapshot);
assert.equal(recovered("storage-filesystem").length, 1);
const beforeDisabled = filesystemCalls;
await monitor.sample({ ...snapshot, directory: null });
assert.equal(filesystemCalls, beforeDisabled);
assert.equal(summary().cache.enabled, false);
assert.equal(warnings("cache-disabled").length, 1);
assert.ok(!JSON.stringify(events).includes("secret"));
assert.ok(!JSON.stringify(events).includes("private payload"));

for (let i = 0; i < 1000; ++i) {
  monitor.providerStarted("host-" + i);
  monitor.providerFinished("host-" + i, {
    failed: true,
    timeout: true,
    status: 429,
  });
}
assert.equal(
  Object.keys(monitor.providers()).length,
  64,
  "Provider cardinality is bounded",
);
assert.ok(monitor.providers().other.requests > 900);
assert.equal(monitor.providers().other.inFlight, 0);

let release;
let samples = 0;
const stalled = new Monitor({
  filesystem: () => {
    ++samples;
    return new Promise((resolve) => {
      release = resolve;
    });
  },
});
const pending = stalled.sample(snapshot);
await stalled.sample(snapshot);
assert.equal(
  samples,
  1,
  "A slow filesystem must not accumulate concurrent samples",
);
release({ bavail: 1000000, bsize: 4096 });
await pending;
console.log(
  "PASS: queue classification, warning thresholds/repeats/recovery, rolling provider counters, bounded hosts, disk sampling and storage failures.",
);
