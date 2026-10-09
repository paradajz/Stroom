import assert from "node:assert/strict";
import { mkdir } from "node:fs/promises";
import { Service, toc, cached } from "./http_service_fixture.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
for (const mode of [
  "none",
  "rejected",
  "failure",
  "limited",
  "malformed",
  "gnudb-read-limited",
  "discogs-limited",
  "write-failure",
  "error-write-failure",
]) {
  const base = Date.now();
  const ttl = ["none", "rejected", "write-failure"].includes(mode)
    ? 86400000
    : mode.endsWith("limited")
      ? 3600000
      : 60000;
  const lifetimeField =
    ttl === 86400000 ? "expires_in_seconds" : "retry_after_seconds";
  const cannotSave = mode.endsWith("write-failure");
  const service = await Service.create("unsuccessful_fixture.mjs", {
    LOOKUP_TEST_NOW: String(base),
    LOOKUP_TEST_MODE: cannotSave
      ? ttl === 86400000
        ? "none"
        : "failure"
      : mode,
  });
  try {
    if (cannotSave)
      await mkdir(service.directory + "/" + discId(toc) + ".json.tmp");
    await service.start();
    let result = await service.complete();
    assert.equal(result.status, ttl === 86400000 ? 200 : 502, mode);
    assert.equal(
      result.body.status,
      ttl === 86400000 ? "no_match" : "error",
      mode,
    );
    assert.equal(result.body[lifetimeField], ttl / 1000, mode);
    if (ttl === 86400000) {
      assert.equal(result.body.retry_after_seconds, undefined);
      assert.equal(result.headers["retry-after"], undefined);
    } else assert.equal(result.body.expires_in_seconds, undefined);
    if (cannotSave)
      await service.until(() =>
        service.output.includes("could not save report:"),
      );
    else {
      const saved = await service.saved(toc);
      assert.equal(saved.lookupRetryAt, base + ttl);
    }
    const before = (
      await service.control({ now: base + 1000, mode: "recovered" })
    ).calls.length;
    for (let i = 0; i < 2; ++i)
      assert.equal((await service.post()).body[lifetimeField], ttl / 1000 - 1);
    assert.equal((await service.control()).calls.length, before);
    const initial = await service.report();
    if (!cannotSave) {
      await service.stop();
      service.env.LOOKUP_TEST_NOW = String(base + 2000);
      service.env.LOOKUP_TEST_MODE = "recovered";
      await service.start();
      assert.equal((await service.post()).body[lifetimeField], ttl / 1000 - 2);
      assert.equal((await service.control()).calls.length, 0);
    }
    await service.control({ now: base + ttl - 1 });
    assert.equal((await service.post()).body[lifetimeField], 1);
    await service.control({ now: base + ttl });
    assert.equal(
      (await service.result()).status,
      404,
      "GET cannot renew expired work",
    );
    result = await service.complete();
    assert.equal(result.body.status, "matched", mode);
    assert.ok((await service.control()).calls.length > 0);
    if (!cannotSave) {
      await service.saved(toc, (r) => !!r?.selected);
      await service.stop();
      await saveReport(service.directory, discId(toc), initial);
      service.env.LOOKUP_TEST_NOW = String(base + ttl);
      await service.start();
      assert.equal((await service.complete()).body.status, "matched");
    }
    await service.stop();
  } finally {
    await service.close();
  }
}
console.log(
  "PASS: no-match/provider failure TTLs, separate expiry/retry fields, deduplication, restart persistence, expiry, GET never renewing work, provider cooldowns and failed saves.",
);

// Administrative refresh bypasses saved outcomes, but not upstream cooldowns.
for (const mode of ["none", "limited"]) {
  const now = Date.now();
  const service = await Service.create("unsuccessful_fixture.mjs", {
    LOOKUP_TEST_NOW: String(now),
    LOOKUP_TEST_MODE: mode,
  });
  try {
    await service.start();
    await service.complete();
    await service.saved(toc);
    await service.stop();
    service.env.LOOKUP_TEST_NOW = String(now + 1000);
    service.env.LOOKUP_TEST_MODE = "recovered";
    await service.start(["--refresh"]);
    const result = await service.complete();
    if (mode === "limited") {
      assert.equal(result.status, 502);
      assert.equal(result.body.retry_after_seconds, 3599);
      assert.ok(!(await service.control()).calls.includes("musicbrainz.org"));
    } else {
      assert.equal(result.body.status, "matched");
      assert.ok((await service.control()).calls.includes("musicbrainz.org"));
    }
    await service.stop();
  } finally {
    await service.close();
  }
}

// The advertised seven-day cap must not shorten the actual provider deadline.
{
  const base = Date.now(),
    week = 604800,
    full = 2 * week * 1000;
  const service = await Service.create("unsuccessful_fixture.mjs", {
    LOOKUP_TEST_NOW: String(base),
    LOOKUP_TEST_MODE: "limited",
    LOOKUP_TEST_RETRY_SECONDS: String(2 * week),
  });
  const artLayout = { ...toc, leadout: toc.leadout + 1 };
  try {
    await saveReport(
      service.directory,
      discId(artLayout),
      cached(artLayout, { artworkRetryable: true, retryAt: base + full }),
    );
    await service.start();
    let response = await service.complete();
    assert.equal(response.status, 502);
    assert.equal(response.body.retry_after_seconds, week);
    assert.equal(Number(response.headers["retry-after"]), week);
    assert.equal((await service.saved(toc)).lookupRetryAt, base + full);
    let art = await service.art(artLayout);
    assert.equal(art.body.artwork.retry_after_seconds, week);
    assert.equal(art.headers["retry-after"], undefined);
    await service.stop();
    service.env.LOOKUP_TEST_NOW = String(base + week * 1000);
    service.env.LOOKUP_TEST_MODE = "recovered";
    await service.start();
    response = await service.post();
    assert.equal(response.status, 502);
    assert.equal(response.body.retry_after_seconds, week);
    assert.equal(
      (await service.control()).calls.length,
      0,
      "A clipped wire delay must not release the provider early after restart",
    );
    await service.control({ now: base + full - 1 });
    assert.equal((await service.post()).body.retry_after_seconds, 1);
    assert.equal(
      (await service.art(artLayout)).body.artwork.retry_after_seconds,
      1,
    );
    await service.control({ now: base + full });
    assert.equal((await service.complete()).body.status, "matched");
    assert.ok((await service.control()).calls.includes("musicbrainz.org"));
    await service.stop();
  } finally {
    await service.close();
  }
}
