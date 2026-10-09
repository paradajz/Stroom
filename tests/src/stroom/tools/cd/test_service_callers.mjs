import assert from "node:assert/strict";
import { readdir } from "node:fs/promises";
import { Service, cached } from "./http_service_fixture.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
const layouts = Array.from({ length: 16 }, (_, i) => ({
  first: 1,
  offsets: [150, 15363 + i],
  leadout: 32314 + i,
}));
const service = await Service.create("caller_fixture.mjs");
const started = () =>
  [...service.output.matchAll(/CALLER_LOOKUP (\d+)/g)].map((m) => Number(m[1]));
const ip = (i) => "127.0.0." + (i + 1);
const post = (i, caller) => service.post(layouts[i], { ip: ip(caller) });
async function deny(i, caller) {
  const result = await post(i, caller);
  assert.equal(result.status, 503);
  assert.equal(result.body.error.code, "busy");
  assert.ok(
    !(await readdir(service.directory)).includes(discId(layouts[i]) + ".json"),
  );
}
async function finish(i) {
  await service.until(() => started().at(-1) === i);
  await service.control({ release: i });
  await service.saved(layouts[i], (r) => !!r?.lookupStatus);
}
try {
  for (const i of [14, 15])
    await saveReport(service.directory, discId(layouts[i]), cached(layouts[i]));
  await service.start();
  assert.equal((await post(0, 0)).status, 202);
  await service.until(() => started().includes(0));
  assert.equal((await post(1, 0)).status, 202);
  assert.equal((await post(1, 0)).status, 202);
  assert.equal(
    (await post(0, 1)).status,
    202,
    "Shared work does not spend another caller's token",
  );
  await deny(4, 0);
  for (const i of [14, 15]) assert.equal((await post(i, 0)).status, 200);
  assert.equal((await post(2, 1)).status, 202);
  await deny(3, 1);
  await deny(5, 1);
  assert.equal(
    (await post(6, 2)).status,
    202,
    "A third caller can join while two keys have waiting work",
  );
  await deny(7, 2);
  await finish(0);
  assert.equal((await post(3, 1)).status, 202);
  await finish(2);
  assert.equal((await post(7, 2)).status, 202);
  for (const i of [6, 1, 3, 7]) await finish(i);
  assert.deepEqual(started(), [0, 2, 6, 1, 3, 7]);
  assert.equal((await service.report(layouts[6])).lookupStatus, "error");
  await deny(4, 0);
  await service.control({ advance: 10000 });
  assert.equal((await post(4, 0)).status, 202);
  await deny(5, 0);
  await service.control({ advance: 10000 });
  assert.equal((await post(5, 0)).status, 202);
  await finish(4);
  await finish(5);
  await service.stop();
} finally {
  await service.close();
}
console.log(
  "PASS: HTTP admission before 202, per-IP outstanding/rate limits, shared jobs, cached reads while limited, fair rotation, slot release and token refill.",
);
