import assert from "node:assert/strict";
import { Service, cached } from "./http_service_fixture.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
const service = await Service.create("service_fixture.mjs", {
  CACHE_OFFLINE: "1",
});
const layout = {
  first: 1,
  offsets: Array.from({ length: 99 }, (_, i) => 150 + i * 4500),
  leadout: 445650,
};
try {
  const report = cached(layout, {
    title: "é".repeat(100),
    trackTitle: (i) => "Track " + i + "\0" + "🎵".repeat(100),
  });
  await saveReport(service.directory, discId(layout), report);
  await service.start();
  for (const ip of ["127.0.0.10", "127.0.0.11", "127.0.0.12"]) {
    const result = await service.post(layout, { ip });
    assert.equal(result.status, 200);
    assert.equal(result.body.track_count, 99);
    assert.equal(result.body.tracks.length, 99);
    for (const track of result.body.tracks) {
      assert.ok(Buffer.byteLength(track.title) <= 127);
      assert.ok(!track.title.includes("\0"));
      assert.ok(!track.title.includes("�"));
    }
    assert.equal(Buffer.byteLength(result.body.album), 126);
    assert.ok(result.bytes.length <= 65536);
  }
  assert.equal((service.output.match(/cache hit/g) ?? []).length, 1);
  assert.ok(!service.output.includes("FIXTURE_REQUEST"));
  await service.stop();
} finally {
  await service.close();
}
console.log(
  "PASS: complete 99-track replies, UTF-8 byte bounds, NUL sanitation, independent clients and a shared cached result.",
);
