import assert from "node:assert/strict";
import { Service, toc } from "./http_service_fixture.mjs";
import { assertCover } from "./cover_fixture.mjs";
for (const status of [503, 404]) {
  const service = await Service.create("service_fixture.mjs", {
    COVER_FAILURE_STATUS: String(status),
  });
  try {
    await service.start();
    assert.equal((await service.complete()).body.status, "matched");
    await service.until(() => service.output.includes("Report:"));
    const before = (service.output.match(/FIXTURE_REQUEST/g) ?? []).length;
    const first = await service.art();
    assert.equal(
      first.body.artwork.status,
      status === 503 ? "pending" : "unavailable",
    );
    assert.ok(
      service.output.includes("artwork " + first.body.artwork.status + ";"),
    );
    assert.equal(
      (service.output.match(/FIXTURE_REQUEST/g) ?? []).length,
      before,
    );
    await service.control("advance");
    let art = await service.art();
    if (status === 503) {
      await service.until(async () => {
        art = await service.art();
        return art.body.artwork.status === "available";
      });
      await assertCover((await service.request(art.body.artwork.path)).bytes);
      assert.equal(
        (service.output.match(/FIXTURE_REQUEST/g) ?? []).length,
        before + 1,
      );
    } else
      assert.equal(
        (service.output.match(/FIXTURE_REQUEST/g) ?? []).length,
        before,
      );
    await service.saved(toc, (r) => r?.artworkRetryable === false);
    await service.stop();
  } finally {
    await service.close();
  }
}
console.log(
  "PASS: HTTP artwork-only recovery, saved cooldowns, completed caching and confirmed missing covers.",
);
