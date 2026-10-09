import assert from "node:assert/strict";
import { writeFile } from "node:fs/promises";
import { Service, cached, toc } from "./http_service_fixture.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";

const service = await Service.create();
service.config = {
  cache_max_size: "8 MB",
  cover_speed: "512 KiB/s",
  log_statistics: true,
};
const cover = discId(toc) + "-cover.jpg";
const bytes = Buffer.alloc(512 * 1024, 42);
try {
  await saveReport(service.directory, discId(toc), cached(toc));
  await writeFile(service.directory + "/" + cover, bytes);
  await service.start();
  await service.until(() => service.summaries().length > 0);
  const summary = service.summaries()[0];
  assert.equal(summary.cache.maxBytes, 8_000_000);
  assert.equal(summary.cache.targetBytes, 7_200_000);
  assert.equal(summary.cache.maxDiscs, 10000);
  assert.equal(summary.cache.maxFiles, 20000);
  assert.equal(summary.memoryCache.maxBytes, 64 * 1024 * 1024);
  assert.equal(summary.http.limits.connections, 64);
  assert.equal(summary.http.limits.connectionsPerIp, 8);
  assert.equal(summary.http.limits.covers, 8);
  assert.equal(summary.http.limits.coversPerIp, 1);
  const start = performance.now();
  const images = [2, 3, 4, 5].map((ip) =>
    service.request("/covers/" + cover, { ip: "127.0.0." + ip }),
  );
  // Metadata stays available while the four images share the configured speed.
  assert.equal((await service.post()).body.status, "matched");
  for (const image of await Promise.all(images)) {
    assert.equal(image.status, 200);
    assert.deepEqual(image.bytes, bytes);
  }
  assert.ok(
    performance.now() - start >= 1800,
    "2 MiB at 512 KiB/s with a fixed 1 MiB burst takes at least 2s",
  );
  await service.stop();
} finally {
  await service.close();
}
console.log(
  "PASS: flat cache and speed settings reach the live service; cleanup targets are derived, protection limits remain fixed and throttling preserves metadata access.",
);
