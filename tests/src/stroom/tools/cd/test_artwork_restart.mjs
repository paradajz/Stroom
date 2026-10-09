import assert from "node:assert/strict";
import { Service, toc, cached } from "./http_service_fixture.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
for (const provider of [
  "unfinished",
  "expired",
  "temporary",
  "image",
  "discogs",
]) {
  const failure = ["temporary", "image", "discogs"].includes(provider);
  const service = await Service.create("service_fixture.mjs", {
    RETRY_TEST: "1",
    COVER_FAILURE_STATUS: failure ? "503" : "",
    RETRY_AFTER: provider === "image" ? "180" : "",
    RETRY_DISCOGS: provider === "discogs" ? "1" : "",
  });
  try {
    const report = cached(toc, {
      artworkRetryable: true,
      retryAt: provider === "expired" ? Date.now() - 1000 : 0,
    });
    report.candidates[0].coverUrl =
      "https://coverartarchive.org/release/test/front-250";
    await saveReport(service.directory, discId(toc), report);
    await service.start();
    assert.equal((await service.post()).body.status, "matched");
    assert.ok(
      !service.output.includes("FIXTURE_REQUEST"),
      "Metadata GET/POST does not start saved artwork work",
    );
    await service.art();
    if (failure) {
      await service.until(() =>
        service.output.includes("retry deferred until"),
      );
      const saved = await service.report();
      const wait = provider === "temporary" ? 60000 : 180000;
      assert.ok(saved.artworkRetryAt > Date.now() + wait - 10000);
      await service.stop();
      service.env.COVER_FAILURE_STATUS = "";
      service.env.RETRY_AFTER = "";
      service.env.RETRY_DISCOGS = "";
      await service.start();
      await service.post();
      await service.control({ advance: wait - 30000 });
      await service.art();
      assert.ok(!service.output.includes("FIXTURE_REQUEST"));
      assert.equal(
        (await service.report()).artworkRetryAt,
        saved.artworkRetryAt,
      );
      await service.control({ advance: 31000 });
      await service.art();
    }
    await service.until(() =>
      service.output.includes("artwork lookup complete"),
    );
    assert.equal((await service.art()).body.artwork.status, "available");
    assert.equal((await service.report()).artworkRetryAt, 0);
    await service.stop();
  } finally {
    await service.close();
  }
}
console.log(
  "PASS: saved artwork state resumes through HTTP status requests, provider cooldowns survive restart and metadata remains immediate.",
);
