import { createServer } from "node:net";
import { once } from "node:events";
import assert from "node:assert/strict";
import { readFile, writeFile, unlink } from "node:fs/promises";
import { Service, toc, cached } from "./http_service_fixture.mjs";
import {
  cacheDirectory,
  cachedReport,
  saveReport,
} from "../../../../../tools/cd/recognition/cache.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { prepareCover } from "../../../../../tools/cd/recognition/image.mjs";
import { coverInput, assertCover } from "./cover_fixture.mjs";
assert.equal(
  cacheDirectory("linux", "/home/user", ""),
  "/home/user/.cache/stroom/cd",
);
assert.equal(
  cacheDirectory("darwin", "/Users/user", ""),
  "/Users/user/Library/Caches/stroom/cd",
);
assert.equal(
  cacheDirectory("darwin", "/Users/user", "/custom"),
  "/custom/stroom/cd",
);
assert.equal(
  cacheDirectory("linux", "/home/user", "relative"),
  "/home/user/.cache/stroom/cd",
);
const service = await Service.create("service_fixture.mjs", {
  CACHE_OFFLINE: "1",
});
const id = discId(toc),
  coverFile = id + "-release.jpg",
  report = cached(toc, { coverFile });
try {
  assert.equal(await cachedReport(service.directory, id), null);
  await writeFile(
    service.directory + "/" + coverFile,
    await prepareCover(coverInput),
  );
  await saveReport(service.directory, id, report);
  const saved = JSON.parse(
    await readFile(service.directory + "/" + id + ".json", "utf8"),
  );
  assert.equal(saved.cacheVersion, 2);
  await writeFile(
    service.directory + "/" + id + ".json",
    JSON.stringify({ ...saved, cacheVersion: 1 }),
  );
  assert.equal(
    await cachedReport(service.directory, id),
    null,
    "Unsupported cache formats are treated as misses",
  );
  await saveReport(service.directory, id, report);
  for (let run = 0; run < 2; ++run) {
    await service.start();
    const result = await service.post();
    assert.equal(result.body.album, "Cached album");
    assert.equal(result.body.artwork.status, "available");
    await assertCover((await service.request(result.body.artwork.path)).bytes);
    assert.ok(!service.output.includes("FIXTURE_REQUEST"));
    await service.stop();
  }
  const blocker = createServer();
  blocker.listen(12890, "0.0.0.0");
  await once(blocker, "listening");
  try {
    await assert.rejects(service.start());
    assert.equal(service.child.exitCode, 1);
    assert.match(service.output, /CD HTTP unavailable:.*EADDRINUSE/);
    assert.equal(
      (await cachedReport(service.directory, id)).candidates[0].coverFile,
      coverFile,
    );
  } finally {
    await new Promise((resolve) => blocker.close(resolve));
  }
  await unlink(service.directory + "/" + coverFile);
  assert.equal(
    (await cachedReport(service.directory, id)).artworkRetryable,
    true,
  );
  await writeFile(service.directory + "/" + coverFile, "broken JPEG");
  assert.equal(
    (await cachedReport(service.directory, id)).candidates[0].coverFile,
    undefined,
  );
  await writeFile(service.directory + "/" + id + ".json", "{broken");
  assert.equal(await cachedReport(service.directory, id), null);
  await writeFile(
    service.directory + "/" + id + ".json",
    JSON.stringify({ ...report, cacheVersion: -1 }),
  );
  assert.equal(await cachedReport(service.directory, id), null);
  await saveReport(service.directory, id, { ...report, selected: null });
  assert.equal(await cachedReport(service.directory, id), null);
} finally {
  await service.close();
}
console.log(
  "PASS: cache paths, offline HTTP restarts, cached covers and damaged/missing cache recovery.",
);
