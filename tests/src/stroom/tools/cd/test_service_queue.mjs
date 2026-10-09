import assert from "node:assert/strict";
import { once } from "node:events";
import { Service, cached, delay } from "./http_service_fixture.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
const layouts = [
  { first: 1, offsets: [150, 4650], leadout: 9150 },
  { first: 1, offsets: [150, 4725], leadout: 9300 },
];
for (const shutdown of [false, true]) {
  const service = await Service.create("queue_fixture.mjs", {
    QUEUE_BLOCK_RECOGNITION: shutdown ? "1" : "",
  });
  try {
    await service.start();
    assert.equal((await service.complete(layouts[0])).body.status, "matched");
    await service.until(() => service.output.includes("QUEUE_WAIT cover-0"));
    assert.equal((await service.post(layouts[1])).status, 202);
    if (shutdown) {
      await service.until(() =>
        service.output.includes("QUEUE_WAIT recognition-1"),
      );
      const pending = await service.result(layouts[1]);
      assert.equal(pending.status, 202);
      assert.equal(pending.headers.location, undefined);
      assert.equal(pending.body.retry_after_seconds, 5);
      // One running recognition plus only three waiting jobs across all callers.
      for (let i = 2; i < 8; ++i) {
        const result = await service.post(
          { first: 1, offsets: [150, 4800 + i], leadout: 9600 + i },
          { ip: "127.0.0." + (i + 1) },
        );
        assert.equal(result.status, i < 5 ? 202 : 503);
      }
      assert.equal(
        (await service.post(layouts[1])).status,
        202,
        "Joining accepted work still succeeds at capacity",
      );
      const layout = { first: 1, offsets: [150, 5000], leadout: 10000 };
      await saveReport(service.directory, discId(layout), cached(layout));
      assert.equal(
        (await service.post(layout)).body.status,
        "matched",
        "Disk reads remain usable while recognition is full",
      );
      const exited = once(service.child, "exit");
      service.child.kill("SIGTERM");
      await delay(40);
      service.child.send("cover-0");
      await service.until(() => service.output.includes("Report:"));
      assert.equal(service.child.exitCode, null);
      service.child.send("recognition-1");
      assert.equal((await exited)[0], 0, service.output);
      assert.ok(!service.output.includes("QUEUE_WAIT cover-1"));
    } else {
      await service.until(
        async () =>
          (await service.result(layouts[1])).body.status === "matched",
      );
      assert.equal(
        (await service.result(layouts[1])).body.tracks[1].title,
        "Disc 1 track 2",
      );
      assert.ok(!service.output.includes("QUEUE_WAIT cover-1"));
      service.child.send("cover-0");
      await service.until(() => service.output.includes("QUEUE_WAIT cover-1"));
      service.child.send("cover-1");
      await service.until(
        () => (service.output.match(/Report:/g) ?? []).length === 2,
      );
      for (const layout of layouts)
        assert.equal(
          (await service.art(layout)).body.artwork.status,
          "available",
        );
      assert.equal((service.output.match(/QUEUE_LOOKUP 1/g) ?? []).length, 1);
      await service.stop();
    }
  } finally {
    await service.close();
  }
}
console.log(
  "PASS: independent HTTP recognition/artwork queues, deduplication, three waiting recognitions, cached reads at capacity and graceful two-worker shutdown.",
);
