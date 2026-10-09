import assert from "node:assert/strict";
import { readdir } from "node:fs/promises";
import { Service, cached } from "./http_service_fixture.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
import { RESULT_CACHE_BYTES } from "../../../../../tools/cd/recognition/report.mjs";
const layouts = Array.from({ length: 69 }, (_, i) => ({
  first: 1,
  offsets: [150],
  leadout: 15000 + i,
}));
const service = await Service.create("result_fixture.mjs");
try {
  for (let i = 0; i < 66; ++i)
    await saveReport(service.directory, discId(layouts[i]), {
      ...cached(layouts[i], {
        title: "A".repeat(1100000),
        trackTitle: () => "Track " + i,
      }),
      raw: { diagnostic: "x".repeat(16384) },
    });
  service.config = { log_statistics: true };
  await service.start();
  for (let i = 0; i < 50; ++i)
    assert.equal(
      (await service.post(layouts[i])).body.tracks[0].title,
      "Track " + i,
    );
  await service.post(layouts[0]);
  for (let i = 50; i < 66; ++i) await service.post(layouts[i]);
  let memory = (await service.sample()).memoryCache;
  assert.equal(memory.maxBytes, RESULT_CACHE_BYTES);
  assert.ok(memory.bytes <= RESULT_CACHE_BYTES);
  assert.ok(memory.reports < 66 && memory.evictions > 0);
  let state = await service.control();
  assert.equal(state.calls, 0);
  assert.equal(state.reads[discId(layouts[0]) + ".json"], 1);
  await service.post(layouts[0]);
  assert.equal(
    (await service.control()).reads[discId(layouts[0]) + ".json"],
    1,
  );
  await service.post(layouts[1]);
  assert.equal(
    (await service.control()).reads[discId(layouts[1]) + ".json"],
    2,
  );
  assert.equal(
    (await readdir(service.directory)).filter((f) => f.endsWith(".json"))
      .length,
    66,
  );
  assert.equal((await service.report(layouts[1])).raw.diagnostic.length, 16384);
  await service.control({ mode: "limit" });
  assert.equal((await service.complete(layouts[66])).status, 502);
  assert.match((await service.report(layouts[66])).error, /64 releases/);
  assert.equal((await service.control()).calls, 1);
  assert.equal((await service.post(layouts[66])).status, 502);
  assert.equal((await service.control()).calls, 1);
  memory = (await service.sample()).memoryCache;
  await service.control({ mode: "match" });
  assert.equal(
    (await service.complete(layouts[67])).body.tracks[0].title,
    "New track",
  );
  await service.saved(layouts[67], (r) => r?.artworkRetryable === false);
  assert.equal(
    (await service.report(layouts[67])).raw.diagnostic.length,
    2 * 1024 * 1024,
  );
  assert.ok((await service.sample()).memoryCache.bytes - memory.bytes < 10000);
  await service.stop();
  // A compact result which cannot fit is delivered from disk after persistence,
  // without a 202 falsely implying a completed result was retained in RAM.
  service.env.RESULT_TEST_RAM_BYTES = "128";
  service.config = { log_statistics: true };
  await service.start();
  await service.control({ mode: "match", gateWrites: true });
  assert.equal((await service.post(layouts[68])).status, 202);
  await service.until(async () => (await service.control()).writes === 1);
  assert.equal((await service.result(layouts[68])).status, 202);
  assert.equal(
    (await service.post(layouts[0])).body.tracks[0].title,
    "Track 0",
  );
  assert.equal((await service.sample()).memoryCache.bytes, 0);
  assert.equal((await service.control()).calls, 1);
  await service.control({ releaseWrites: true });
  await service.until(() => service.output.includes("Report:"));
  await service.control({ memoryBytes: RESULT_CACHE_BYTES });
  assert.equal(
    (await service.post(layouts[68])).body.tracks[0].title,
    "New track",
  );
  await service.art(layouts[68]);
  await service.saved(layouts[68], (r) => r?.artworkRetryable === false);
  assert.equal(
    (await service.report(layouts[68])).raw.diagnostic.length,
    2 * 1024 * 1024,
  );
  await service.stop();
} finally {
  await service.close();
}
console.log(
  "PASS: HTTP results respect the 64 MiB RAM budget, LRU and disk reload, compact diagnostics, oversized provider backoff and failed RAM admission during persistence.",
);
