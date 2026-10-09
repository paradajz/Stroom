import assert from "node:assert/strict";
import { mkdir, writeFile } from "node:fs/promises";
import { Service, toc } from "./http_service_fixture.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { assertCover } from "./cover_fixture.mjs";
for (const stage of ["startup", "initial", "completed", "retry"]) {
  const service = await Service.create("service_fixture.mjs", {
    COVER_FAILURE_STATUS: stage === "retry" || stage === "startup" ? "503" : "",
  });
  try {
    if (stage === "startup") {
      await writeFile(service.directory + "/blocked", "file");
      service.outputDirectory = service.directory + "/blocked/cache";
    }
    const block = () =>
      mkdir(service.directory + "/" + discId(toc) + ".json.tmp");
    if (stage === "initial") await block();
    await service.start();
    const labels = await service.complete();
    assert.equal(labels.body.tracks[0].title, "First track");
    if (stage === "startup") {
      assert.equal(labels.body.artwork.status, "unavailable");
      assert.match(service.output, /Cache unavailable/);
      assert.equal(
        (await service.result()).body.tracks[1].title,
        "Second track",
      );
      assert.equal((service.output.match(/FIXTURE_REQUEST/g) ?? []).length, 1);
    } else {
      if (stage === "retry") {
        await service.until(() => service.output.includes("Report:"));
        await block();
        await service.control("advance");
        await service.art();
      } else {
        await service.until(() =>
          service.output.includes("FIXTURE_COVER_WAIT"),
        );
        if (stage === "completed") await block();
        service.child.send("release-cover");
      }
      await service.until(() =>
        service.output.includes("could not save report:"),
      );
      let art;
      await service.until(async () => {
        art = await service.art();
        return art.body.artwork.status === "available";
      });
      await assertCover((await service.request(art.body.artwork.path)).bytes);
      assert.equal((await service.result()).body.status, "matched");
      assert.ok(!service.output.includes("lookup failed:"));
    }
    await service.stop();
  } finally {
    await service.close();
  }
}
console.log(
  "PASS: HTTP metadata survives cache initialization and report-write failures, completed artwork and artwork-only retries.",
);
