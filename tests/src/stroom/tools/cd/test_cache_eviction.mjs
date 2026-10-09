import assert from "node:assert/strict";
import { writeFile } from "node:fs/promises";
import { Service, cached } from "./http_service_fixture.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
const service = await Service.create("result_fixture.mjs");
const layout = (i) => ({
  first: 1,
  offsets: [150, 15000 + i],
  leadout: 32000 + i,
});
try {
  await writeFile(
    service.directory + "/provider-cooldowns.json",
    JSON.stringify(
      Object.fromEntries(
        ["musicbrainz.org", "db.cue.tools", "gnudb.gnudb.org"].map((host) => [
          host,
          Date.now() + 600000,
        ]),
      ),
    ),
  );
  await saveReport(service.directory, discId(layout(129)), cached(layout(129)));
  await service.start();
  for (let i = 1; i <= 128; ++i)
    assert.equal(
      (await service.complete(layout(i), { ip: "127.0.0." + i })).status,
      502,
    );
  assert.equal((await service.post(layout(129))).body.status, "matched");
  const before = (service.output.match(/lookup failed:/g) ?? []).length;
  assert.equal(before, 128);
  assert.equal((await service.post(layout(1))).status, 502);
  assert.equal(
    (service.output.match(/lookup failed:/g) ?? []).length,
    before,
    "Eviction retains saved failure cooldowns",
  );
  assert.ok(service.output.includes(discId(layout(1)) + ": cache hit"));
  await service.stop();
} finally {
  await service.close();
}
console.log(
  "PASS: HTTP reads evict idle failed jobs and restore their saved backoff without new provider requests.",
);
