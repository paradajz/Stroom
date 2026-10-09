import assert from "node:assert/strict";
import { Service, toc } from "./http_service_fixture.mjs";
import { assertCover } from "./cover_fixture.mjs";
const service = await Service.create();
try {
  await service.start();
  const probe = await service.request("/cd/v2/service");
  assert.deepEqual(probe.body, {
    protocol: 2,
    service: "stroom-cd-recognition",
    ready: true,
  });
  assert.ok(!service.output.includes("FIXTURE_REQUEST"));
  const first = await service.post();
  assert.equal(first.status, 202);
  assert.equal(first.headers.location, undefined);
  assert.equal(first.body.result_path, "/cd/v2/lookups/" + first.body.disc_id);
  assert.equal(
    first.headers["retry-after"],
    String(first.body.retry_after_seconds),
  );
  await service.until(() => service.output.includes("FIXTURE_COVER_WAIT"));
  const labels = await service.result();
  assert.equal(labels.status, 200);
  assert.equal(labels.body.track_count, 2);
  assert.deepEqual(
    labels.body.tracks.map((t) => t.title),
    ["First track", "Second track"],
  );
  assert.equal(labels.body.artwork.status, "pending");
  const during = await service.result();
  assert.equal(during.body.artwork.status, "pending");
  assert.equal(during.headers["retry-after"], undefined);
  assert.equal(during.body.status, "matched");
  assert.equal(during.body.artwork.status_path, undefined);
  assert.equal(
    (await service.post(toc, { ip: "127.0.0.240" })).body.status,
    "matched",
  );
  assert.equal((service.output.match(/FIXTURE_REQUEST/g) ?? []).length, 2);
  service.child.send("release-cover");
  await service.until(() => service.output.includes("Report:"));
  const artwork = await service.art();
  assert.equal(artwork.body.artwork.status, "available");
  assert.equal(artwork.body.status, "matched");
  const image = await service.request(artwork.body.artwork.path);
  assert.equal(image.headers["content-type"], "image/jpeg");
  await assertCover(image.bytes);
  const saved = await service.report();
  assert.equal(saved.match, "exact");
  assert.ok(saved.candidates[0].coverFile);
  const results = service.output
    .split("\n")
    .filter((line) => line.includes("Report:"));
  assert.equal(results.length, 1);
  assert.match(
    results[0],
    /EXACT: Fixture artist — Fixture album; disc 1, 2 tracks; artwork available; \d+ ms; Report:/,
  );
  assert.ok(!service.output.includes("BEST MATCH"));
  assert.ok(!service.output.includes("Closest disc"));
  assert.ok(!service.output.includes("First track"));
  assert.ok(!service.output.includes("Second track"));
  assert.equal(saved.candidates[0].media[0].tracks[0].title, "First track");
  assert.equal(saved.candidates[0].comparison.trackCountDifference, 0);
  assert.ok(saved.raw.releases.length);
  await service.stop();
} finally {
  await service.close();
}
console.log(
  "PASS: HTTP probe, admitted lookup, whole-disc early labels, deduplication, independent artwork status and JPEG download, persistence and shutdown.",
);
