import assert from "node:assert/strict";
import {
  mkdir,
  open,
  writeFile,
  stat,
  readdir,
  utimes,
  readFile,
} from "node:fs/promises";
import { Service, cached } from "./http_service_fixture.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { CACHE_MAX_BYTES } from "../../../../../tools/cd/recognition/quota.mjs";
import { prepareCover } from "../../../../../tools/cd/recognition/image.mjs";
import { coverInput, assertCover } from "./cover_fixture.mjs";
const layouts = [
    { first: 1, offsets: [150, 15363], leadout: 32314 },
    { first: 1, offsets: [150, 15364], leadout: 32315 },
  ],
  ids = layouts.map(discId);
async function sparse(path, size, time) {
  const file = await open(path, "w");
  try {
    await file.truncate(size);
  } finally {
    await file.close();
  }
  await utimes(path, time, time);
}
for (const stage of ["startup", "report-write", "cover-write"]) {
  const service = await Service.create();
  const providerState = JSON.stringify(
    stage === "report-write"
      ? {}
      : Object.fromEntries(
          ["musicbrainz.org", "db.cue.tools", "gnudb.gnudb.org"].map((host) => [
            host,
            Date.now() + 600000,
          ]),
        ),
  );
  try {
    await writeFile(
      service.directory + "/provider-cooldowns.json",
      providerState,
    );
    if (stage === "startup") {
      await sparse(
        service.directory + "/" + ids[0] + "-orphan.jpg",
        600000000,
        1,
      );
      await sparse(
        service.directory + "/" + ids[1] + "-orphan.jpg",
        500000000,
        2,
      );
    } else if (stage === "report-write") {
      await sparse(
        service.directory + "/" + ids[0] + "-orphan.jpg",
        450000000,
        1,
      );
      await sparse(
        service.directory + "/" + ids[1] + "-orphan.jpg",
        CACHE_MAX_BYTES - 100 - 450000000,
        2,
      );
    } else {
      for (let i = 0; i < 2; ++i) {
        const report = cached(layouts[i], { artworkRetryable: i === 0 });
        report.candidates[0].coverUrl =
          "https://coverartarchive.org/release/test/front-250";
        await saveReport(service.directory, ids[i], report);
      }
      const reportBytes =
        (await stat(service.directory + "/" + ids[0] + ".json")).size +
        (await stat(service.directory + "/" + ids[1] + ".json")).size;
      const jpegBytes = (await prepareCover(coverInput)).length;
      await sparse(
        service.directory + "/" + ids[0] + "-orphan.jpg",
        450000000,
        1,
      );
      await sparse(
        service.directory + "/" + ids[1] + "-orphan.jpg",
        CACHE_MAX_BYTES - reportBytes - jpegBytes + 1 - 450000000,
        2,
      );
      for (let i = 0; i < 2; ++i)
        await utimes(service.directory + "/" + ids[i] + ".json", i + 1, i + 1);
      await mkdir(service.directory + "/" + ids[0] + ".json.tmp");
    }
    await service.start();
    if (stage === "startup") {
      assert.ok(service.output.includes(ids[0] + ": disk cache evicted"));
      assert.deepEqual(
        (await readdir(service.directory)).sort(),
        [
          ids[1] + "-orphan.jpg",
          "provider-cooldowns.json",
          "service-config",
        ].sort(),
      );
    } else {
      if (stage === "cover-write")
        assert.equal((await service.post(layouts[1])).body.status, "matched");
      const result = await service.complete(layouts[0]);
      assert.equal(result.body.status, "matched");
      if (stage === "cover-write") await service.art(layouts[0]);
      await service.until(() => service.output.includes("FIXTURE_COVER_WAIT"));
      if (stage === "report-write")
        assert.ok(service.output.includes(ids[1] + ": disk cache evicted"));
      service.child.send("release-cover");
      let art;
      await service.until(async () => {
        art = await service.art(layouts[0]);
        return art.body.artwork.status === "available";
      });
      await assertCover((await service.request(art.body.artwork.path)).bytes);
      assert.ok(service.output.includes(ids[1] + ": disk cache evicted"));
      assert.ok(
        (await readdir(service.directory)).includes(ids[0] + "-orphan.jpg"),
      );
      if (stage === "cover-write") {
        assert.ok(service.output.includes("could not save report:"));
        assert.equal(
          (await service.complete(layouts[1])).status,
          502,
          "Disk eviction also removes stale RAM reports",
        );
      }
    }
    assert.equal(
      await readFile(service.directory + "/provider-cooldowns.json", "utf8"),
      providerState,
    );
    await service.stop();
  } finally {
    await service.close();
  }
}
console.log(
  "PASS: HTTP cache quotas at startup and report/cover writes, active work protection, stale RAM invalidation, failed saves and preserved provider cooldowns.",
);
