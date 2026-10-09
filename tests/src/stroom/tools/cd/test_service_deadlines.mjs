import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { Service, serviceConfig } from "./http_service_fixture.mjs";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { cd } from "../../../../../tools/contracts/load.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
const directory = await mkdtemp("/tmp/stroom-service-deadlines-");
const tocs = Array.from({ length: 4 }, (_, index) => ({
  first: 1,
  offsets: [150],
  leadout: 15000 + index,
}));
const ids = tocs.map(discId);
for (const index of [2, 3])
  await saveReport(directory, ids[index], {
    discId: ids[index],
    toc: tocs[index],
    selected: { releaseId: "cached", mediumPosition: 1 },
    candidates: [
      {
        id: "cached",
        source: "musicbrainz",
        title: "Album",
        artist: "Artist",
        media: [
          { position: 1, tracks: [{ number: "1", title: "Track " + index }] },
        ],
        coverUrl:
          index === 2
            ? "https://limited.deadline.test/cover"
            : "https://next.deadline.test/cover",
        coverUrls: index === 2 ? ["https://slow.deadline.test/cover"] : [],
        coverAvailable: true,
      },
    ],
    artworkRetryable: true,
    artworkRetryAt: 0,
  });
const child = spawn(
  process.execPath,
  [
    "--import",
    fileURLToPath(new URL("./deadline_fixture.mjs", import.meta.url)),
    fileURLToPath(
      new URL(
        "../../../../../tools/cd/recognition/service.mjs",
        import.meta.url,
      ),
    ),
    "--config",
    await serviceConfig(directory),
  ],
  { stdio: ["ignore", "pipe", "pipe", "ipc"] },
);
let output = "",
  token = 0;
child.stdout.on("data", (data) => (output += data));
child.stderr.on("data", (data) => (output += data));
const api = new Service();
api.peer = 1;
async function until(check) {
  const end = Date.now() + 8000;
  while (!(await check())) {
    assert.equal(child.exitCode, null, output);
    assert.ok(Date.now() < end, output);
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
}
async function control(message = {}) {
  const reply = once(child, "message");
  child.send(message);
  return (await reply)[0];
}
const send = (index) => api.post(tocs[index]);
async function report(index) {
  try {
    return JSON.parse(
      await readFile(directory + "/" + ids[index] + ".json", "utf8"),
    );
  } catch {
    return null;
  }
}
try {
  await until(() => output.includes("CD recognition listening"));
  await send(0);
  await until(() => output.includes("DEADLINE_STALL recognition"));
  await send(1);
  await until(() => output.includes(ids[1] + ": lookup queued"));
  let state = await control();
  assert.equal(
    state.starts.recognition,
    1,
    "Queued time does not start a worker deadline",
  );
  await control({ expire: "recognition" });
  await until(async () => (await report(1))?.lookupStatus === "no-match");
  const failure = await report(0);
  assert.equal(failure.lookupStatus, "error");
  assert.match(failure.error, /60-second deadline/);
  assert.ok(failure.lookupRetryAt >= Date.now() + 50000);
  state = await control();
  assert.deepEqual(state.aborted, ["recognition"]);
  assert.equal(state.requests.filter((url) => url.includes(ids[0])).length, 1);
  assert.equal(state.starts.recognition, 2);
  assert.deepEqual(state.active, []);
  // Saved unsuccessful outcomes are reused immediately rather than retried.
  await send(0);
  await until(() => output.includes(ids[0] + ": lookup failed"));
  assert.equal((await control()).starts.recognition, 2);

  const first = await send(2);
  assert.equal(first.body.tracks[0].title, "Track 2");
  await api.art(tocs[2]);
  await until(() => output.includes("DEADLINE_STALL artwork"));
  const next = await send(3);
  assert.equal(next.body.tracks[0].title, "Track 3");
  await api.art(tocs[3]);
  state = await control();
  assert.equal(
    state.starts.artwork,
    1,
    "Waiting artwork receives a fresh budget when its worker starts",
  );
  const retryAfter = Date.now() + 3500000;
  await control({ expire: "artwork" });
  await until(async () => (await report(2))?.artworkRetryAt > retryAfter);
  await until(async () => (await report(3))?.artworkRetryable === false);
  const retained = await report(2);
  assert.equal(retained.selected.releaseId, "cached");
  assert.equal(retained.artworkRetryable, true);
  assert.equal(retained.candidates[0].coverFile, undefined);
  assert.deepEqual(
    retained.candidates[0].coverRejectedUrls,
    [],
    "Timeouts must not permanently reject an image URL",
  );
  const again = await send(2);
  assert.equal(again.body.tracks[0].title, "Track 2");
  state = await control();
  assert.deepEqual(state.aborted, ["recognition", "artwork"]);
  assert.equal(state.starts.artwork, 2);
  assert.deepEqual(state.active, []);
  assert.ok(output.includes("120-second deadline"));
  assert.ok(!output.includes("UnhandledPromiseRejection"));
  console.log("CD service whole-job deadline checks passed");
} finally {
  const exited = once(child, "exit");
  child.kill("SIGKILL");
  await exited;
  await rm(directory, { recursive: true, force: true });
}
