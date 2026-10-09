import assert from "node:assert/strict";
import {
  lstat,
  mkdir,
  mkdtemp,
  open,
  readdir,
  rm,
  symlink,
  utimes,
} from "node:fs/promises";
import { join } from "node:path";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import {
  CACHE_MAX_BYTES,
  CACHE_TARGET_BYTES,
  CACHE_MAX_DISCS,
  CACHE_TARGET_DISCS,
  CACHE_MAX_FILES,
  CACHE_TARGET_FILES,
  DiskCacheQuota,
} from "../../../../../tools/cd/recognition/quota.mjs";

assert.equal(CACHE_MAX_BYTES, 1_000_000_000);
assert.equal(CACHE_TARGET_BYTES, 900_000_000);
assert.equal(CACHE_MAX_DISCS, 10000);
assert.equal(CACHE_TARGET_DISCS, 9000);
assert.equal(CACHE_MAX_FILES, 20000);
assert.equal(CACHE_TARGET_FILES, 18000);
const countLimits = {
  maxDiscs: CACHE_MAX_DISCS,
  targetDiscs: CACHE_TARGET_DISCS,
  maxFiles: CACHE_MAX_FILES,
  targetFiles: CACHE_TARGET_FILES,
};
for (const options of [
  { maxDiscs: 2, targetDiscs: 2 },
  { maxFiles: 2, targetFiles: -1 },
  { maxDiscs: 2.5, targetDiscs: 1 },
  { maxFiles: Infinity },
])
  assert.throws(() => new DiskCacheQuota(null, options), /count targets/);
const root = await mkdtemp("/tmp/stroom-cache-quota-");
const ids = Array.from({ length: 4 }, (_, i) =>
  discId({ first: 1, offsets: [150], leadout: 15000 + i }),
);
async function file(directory, name, bytes, time = 1) {
  const path = join(directory, name);
  const fd = await open(path, "w");
  try {
    await fd.truncate(bytes);
  } finally {
    await fd.close();
  }
  await utimes(path, time, time);
}
async function scenario(name) {
  const directory = join(root, name);
  await mkdir(directory);
  return directory;
}
const quota = (directory, options = {}) =>
  new DiskCacheQuota(directory, {
    maxBytes: 1000,
    targetBytes: 900,
    onError: (error) => {
      throw error;
    },
    ...options,
  });
const names = async (directory) => (await readdir(directory)).sort();
try {
  // Startup counts report remnants and orphaned covers, but never follows
  // symlinks, traverses directories or removes unrelated/provider state.
  {
    const directory = await scenario("startup");
    await file(directory, ids[0] + ".json", 100);
    await file(directory, ids[0] + "-release.jpg", 200);
    await file(directory, ids[0] + ".json.tmp", 50);
    await file(directory, ids[1] + "-orphan.jpg", 350, 2);
    await file(directory, ids[2] + ".json", 350, 3);
    await file(directory, "provider-cooldowns.json", 2000);
    await file(directory, "provider-cooldowns.json.tmp", 2000);
    await file(directory, "notes.json", 2000);
    await file(directory, "cover.jpg", 2000);
    await mkdir(join(directory, ids[3] + ".json.tmp"));
    await symlink("notes.json", join(directory, ids[3] + ".json"));
    await quota(directory).initialize();
    assert.deepEqual(
      await names(directory),
      [
        ids[1] + "-orphan.jpg",
        ids[2] + ".json",
        ids[3] + ".json",
        ids[3] + ".json.tmp",
        "cover.jpg",
        "notes.json",
        "provider-cooldowns.json",
        "provider-cooldowns.json.tmp",
      ].sort(),
    );
    assert.ok(
      (await lstat(join(directory, ids[3] + ".json"))).isSymbolicLink(),
    );
  }
  // Exactly the upper limit is allowed. Once exceeded, eviction continues
  // past the upper limit until reaching the lower target, in whole disc groups.
  {
    const directory = await scenario("thresholds");
    await file(directory, ids[0] + ".json", 50);
    await file(directory, ids[1] + ".json", 200, 2);
    await file(directory, ids[2] + ".json", 750, 3);
    const cache = quota(directory);
    await cache.initialize();
    assert.equal((await names(directory)).length, 3);
    assert.deepEqual(cache.snapshot(), {
      bytes: 1000,
      maxBytes: 1000,
      targetBytes: 900,
      discs: 3,
      files: 3,
      ...countLimits,
      evictions: 0,
    });
    await file(directory, ids[3] + ".json", 100, 4);
    await cache.record(ids[3] + ".json");
    assert.deepEqual(cache.snapshot(), {
      bytes: 850,
      maxBytes: 1000,
      targetBytes: 900,
      discs: 2,
      files: 2,
      ...countLimits,
      evictions: 2,
    });
    assert.deepEqual(
      await names(directory),
      [ids[2] + ".json", ids[3] + ".json"].sort(),
    );
  }
  // Access changes the eviction order, and that order survives a fresh scan.
  {
    const directory = await scenario("recency");
    for (let i = 0; i < 3; ++i)
      await file(directory, ids[i] + ".json", 300, i + 1);
    const cache = quota(directory);
    await cache.initialize();
    await cache.touch(ids[0]);
    const restarted = quota(directory);
    await restarted.initialize();
    await file(directory, ids[3] + ".json", 200, Date.now() / 1000);
    await restarted.record(ids[3] + ".json");
    assert.deepEqual(
      await names(directory),
      [ids[0] + ".json", ids[2] + ".json", ids[3] + ".json"].sort(),
    );
  }
  // Replacing files adjusts accounting rather than counting both versions.
  {
    const directory = await scenario("overwrite");
    await file(directory, ids[0] + ".json", 500, 2);
    await file(directory, ids[1] + ".json", 400);
    const cache = quota(directory);
    await cache.initialize();
    await file(directory, ids[0] + ".json", 600, 3);
    await Promise.all([
      cache.record(ids[0] + ".json"),
      cache.record(ids[0] + ".json"),
    ]);
    assert.equal((await names(directory)).length, 2);
    assert.equal(cache.snapshot().files, 2);
    await file(directory, ids[0] + ".json", 601, 4);
    await cache.record(ids[0] + ".json");
    assert.deepEqual(await names(directory), [ids[0] + ".json"]);
    assert.equal(cache.snapshot().files, 1);
  }
  // Busy entries can exceed the quota temporarily. Releasing a reader resumes
  // cleanup, including the outstanding lower target after crossing below 1 GB.
  {
    const directory = await scenario("protected");
    await file(directory, ids[0] + ".json", 120);
    await file(directory, ids[1] + ".json", 950, 2);
    const protectedIds = new Set([ids[1]]);
    const cache = quota(directory, {
      isProtected: (id) => protectedIds.has(id),
    });
    const release = cache.hold(ids[0]);
    await cache.initialize();
    assert.equal((await names(directory)).length, 2);
    release();
    await cache.idle();
    assert.deepEqual(await names(directory), [ids[1] + ".json"]);
    protectedIds.clear();
    await cache.prune();
    assert.deepEqual(await names(directory), []);
  }
  // Diagnostics retain failed deletions and only report recovery after cleanup
  // succeeds, without counting a failed eviction as a removed disc.
  {
    const directory = await scenario("delete-recovery");
    await file(directory, ids[0] + ".json", 200);
    await file(directory, ids[1] + ".json", 900, 2);
    const protectedIds = new Set([ids[0], ids[1]]);
    const failures = [],
      successes = [];
    const cache = quota(directory, {
      isProtected: (id) => protectedIds.has(id),
      onError: (error, operation) =>
        failures.push({ code: error.code, operation }),
      onSuccess: (operation) => successes.push(operation),
    });
    await cache.initialize();
    await rm(join(directory, ids[0] + ".json"));
    await mkdir(join(directory, ids[0] + ".json"));
    protectedIds.delete(ids[0]);
    await cache.prune();
    assert.deepEqual(failures, [{ code: "EISDIR", operation: "delete" }]);
    assert.equal(successes.includes("delete"), false);
    assert.equal(cache.snapshot().evictions, 0);
    await rm(join(directory, ids[0] + ".json"), { recursive: true });
    await file(directory, ids[0] + ".json", 200);
    await cache.prune();
    assert.equal(successes.at(-1), "delete");
    assert.equal(cache.snapshot().bytes, 900);
    assert.equal(cache.snapshot().evictions, 1);
  }
  // Crossing one upper limit must meet every lower target.
  {
    const directory = await scenario("combined-targets");
    for (let i = 0; i < 4; ++i)
      await file(directory, ids[i] + ".json", 250, i + 1);
    const cache = quota(directory, {
      maxDiscs: 4,
      targetDiscs: 2,
      maxFiles: 5,
      targetFiles: 3,
    });
    await cache.initialize();
    assert.equal(cache.snapshot().discs, 4);
    await file(directory, ids[3] + "-front.jpg", 1, 4);
    await cache.record(ids[3] + "-front.jpg");
    assert.equal(
      cache.snapshot().discs,
      2,
      "A byte-triggered prune must also meet the disc target",
    );
    assert.equal(cache.snapshot().files, 3);
    assert.equal(cache.snapshot().bytes, 501);
  }
  // Tiny reports trigger disc-count eviction independently of byte usage.
  {
    const directory = await scenario("disc-count");
    for (let i = 0; i < 3; ++i)
      await file(directory, ids[i] + ".json", 1, i + 1);
    const cache = quota(directory, { maxDiscs: 3, targetDiscs: 2 });
    await cache.initialize();
    assert.equal(
      cache.snapshot().discs,
      3,
      "The upper count limit is inclusive",
    );
    await file(directory, ids[3] + ".json", 1, 4);
    await cache.record(ids[3] + ".json");
    assert.equal(cache.snapshot().discs, 2);
    assert.equal(cache.snapshot().files, 2);
    assert.equal(cache.snapshot().bytes, 2);
    assert.deepEqual(
      await names(directory),
      [ids[2] + ".json", ids[3] + ".json"].sort(),
    );
    const startup = quota(directory, { maxDiscs: 1, targetDiscs: 0 });
    await startup.initialize();
    assert.equal(
      startup.snapshot().discs,
      0,
      "Existing files also obey count limits",
    );
  }
  // Reports, extra covers and temporary files all count, even when empty.
  {
    const directory = await scenario("file-count");
    for (const suffix of [".json", ".json.tmp", "-front.jpg", "-other.jpg"])
      await file(directory, ids[0] + suffix, 0);
    await file(directory, ids[1] + ".json", 1, 2);
    await file(directory, "provider-cooldowns.json", 1);
    const cache = quota(directory, { maxFiles: 4, targetFiles: 3 });
    await cache.initialize();
    assert.equal(cache.snapshot().files, 1);
    assert.equal(cache.snapshot().discs, 1);
    assert.equal(cache.snapshot().bytes, 1);
    assert.equal(cache.snapshot().evictions, 1);
    assert.deepEqual(
      await names(directory),
      [ids[1] + ".json", "provider-cooldowns.json"].sort(),
    );
  }
  {
    const directory = await scenario("file-count-write");
    await file(directory, ids[0] + ".json", 0);
    await file(directory, ids[0] + "-front.jpg", 0);
    await file(directory, ids[1] + ".json", 1, 2);
    const cache = quota(directory, { maxFiles: 3, targetFiles: 2 });
    await cache.initialize();
    await cache.record(ids[0] + ".json", ids[0] + ".json");
    assert.equal(
      cache.snapshot().files,
      3,
      "Repeated accounting cannot count a file twice",
    );
    await file(directory, ids[0] + ".json.tmp", 0);
    await cache.record(ids[0] + ".json.tmp");
    assert.deepEqual(await names(directory), [ids[1] + ".json"]);
    await rm(join(directory, ids[1] + ".json"));
    await cache.record(ids[1] + ".json");
    assert.equal(cache.snapshot().files, 0);
    assert.equal(cache.snapshot().discs, 0);
    assert.equal(cache.snapshot().bytes, 0);
  }
  // Count-triggered cleanup remains pending below the upper limits until all
  // lower targets are met, without evicting active jobs or outstanding readers.
  {
    const directory = await scenario("count-protection");
    for (let i = 0; i < 4; ++i)
      await file(directory, ids[i] + ".json", 1, i + 1);
    const protectedIds = new Set([ids[2], ids[3]]);
    const cache = quota(directory, {
      maxDiscs: 3,
      targetDiscs: 1,
      maxFiles: 3,
      targetFiles: 1,
      isProtected: (id) => protectedIds.has(id),
    });
    const first = cache.hold(ids[0]),
      second = cache.hold(ids[1]);
    await cache.initialize();
    assert.equal(cache.snapshot().files, 4);
    first();
    await cache.idle();
    assert.equal(cache.snapshot().files, 3);
    second();
    second();
    await cache.idle();
    assert.equal(cache.snapshot().files, 2);
    protectedIds.delete(ids[2]);
    await cache.prune();
    assert.equal(cache.snapshot().files, 1);
    assert.equal(cache.snapshot().discs, 1);
    assert.deepEqual(await names(directory), [ids[3] + ".json"]);
  }
  // Partial deletion failures retain both byte and file accounting until retry.
  {
    const directory = await scenario("count-delete-recovery");
    await file(directory, ids[0] + ".json", 1);
    await file(directory, ids[0] + ".json.tmp", 0);
    await file(directory, ids[1] + ".json", 1, 2);
    const failures = [];
    const cache = quota(directory, {
      maxFiles: 3,
      targetFiles: 2,
      isProtected: (id) => id === ids[1],
      onError: (error) => failures.push(error.code),
    });
    await cache.initialize();
    await rm(join(directory, ids[0] + ".json"));
    await mkdir(join(directory, ids[0] + ".json"));
    await file(directory, ids[1] + "-front.jpg", 0, 2);
    await cache.record(ids[1] + "-front.jpg");
    assert.deepEqual(failures, ["EISDIR"]);
    assert.equal(cache.snapshot().files, 3);
    assert.equal(cache.snapshot().bytes, 2);
    assert.equal(cache.snapshot().evictions, 0);
    await rm(join(directory, ids[0] + ".json"), { recursive: true });
    await cache.prune();
    assert.equal(cache.snapshot().files, 2);
    assert.equal(cache.snapshot().bytes, 1);
    assert.equal(cache.snapshot().evictions, 1);
  }
  console.log(
    "PASS: quota thresholds, grouped LRU eviction, persisted access times, incremental accounting, protected entries and unrelated-file preservation.",
  );
} finally {
  await rm(root, { recursive: true, force: true });
}
