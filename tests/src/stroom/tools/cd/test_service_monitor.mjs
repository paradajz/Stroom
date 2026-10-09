import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { Service, serviceConfig } from "./http_service_fixture.mjs";
import { mkdtemp, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { cd } from "../../../../../tools/contracts/load.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";

const quiet = await Service.create("monitor_fixture.mjs");
try {
  await quiet.start();
  await quiet.control({ freeBytes: 200_000_000, sample: true });
  await quiet.until(() =>
    quiet.output.includes('"event":"critical","name":"disk-free"'),
  );
  await quiet.control({ freeBytes: 8_000_000_000, sample: true });
  await quiet.until(() =>
    quiet.output.includes('"event":"recovered","name":"disk-free"'),
  );
  assert.deepEqual(quiet.summaries(), []);
  await quiet.stop();
} finally {
  await quiet.close();
}

const directory = await mkdtemp("/tmp/stroom-monitor-");
const discs = Array.from({ length: 4 }, (_, index) => ({
  first: 1,
  offsets: [150, 15363 + index],
  leadout: 32314 + index,
}));
const ids = discs.map(discId);
for (const index of [2, 3])
  await saveReport(directory, ids[index], {
    discId: ids[index],
    toc: discs[index],
    selected: { releaseId: "cached-" + index, mediumPosition: 1 },
    artworkRetryable: true,
    candidates: [
      {
        id: "cached-" + index,
        artist: "Artist",
        artists: ["Artist"],
        title: "Album",
        coverUrl: "https://images.test/" + index,
        media: [
          { position: 1, tracks: [{ title: "First" }, { title: "Second" }] },
        ],
      },
    ],
  });
const child = spawn(
  process.execPath,
  [
    "--import",
    fileURLToPath(new URL("./monitor_fixture.mjs", import.meta.url)),
    fileURLToPath(
      new URL(
        "../../../../../tools/cd/recognition/service.mjs",
        import.meta.url,
      ),
    ),
    "--config",
    await serviceConfig(directory, { log_statistics: true }),
  ],
  { stdio: ["ignore", "pipe", "pipe", "ipc"] },
);
let output = "",
  token = 0;
child.stdout.on("data", (bytes) => (output += bytes));
child.stderr.on("data", (bytes) => (output += bytes));
const api = new Service();
api.peer = 1;
const records = () =>
  output
    .split("\n")
    .filter((line) => line.startsWith("CD monitor: "))
    .flatMap((line) => {
      try {
        return [JSON.parse(line.slice("CD monitor: ".length))];
      } catch {
        return [];
      }
    });
const summaries = () => records().filter((event) => event.event === "summary");
async function until(check) {
  const deadline = Date.now() + 5000;
  while (!check()) {
    assert.equal(child.exitCode, null, output);
    assert.ok(Date.now() < deadline, output);
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
}
async function control(message) {
  const ack = once(child, "message");
  child.send(message);
  assert.equal((await ack)[0], "ack");
}
async function sample() {
  const before = summaries().length;
  await control({ sample: true });
  await until(() => summaries().length > before);
  return summaries().at(-1);
}
async function send(index) {
  await api.post(discs[index]);
  if (index >= 2) await api.art(discs[index]);
  await until(() =>
    output.includes(
      ids[index] + (index < 2 ? ": lookup queued" : ": cache hit"),
    ),
  );
}
try {
  await until(() => summaries().length === 1);
  assert.deepEqual(summaries()[0].recognition, {
    waiting: 0,
    running: 0,
    oldestWaitingMs: 0,
  });
  assert.equal(summaries()[0].storage.freeBytes, 8_000_000_000);
  await send(0);
  await until(() => output.includes("MONITOR_GATE lookup-0"));
  await send(1);
  await send(2);
  await until(() => output.includes("MONITOR_GATE cover-2"));
  await send(3);
  let current = await sample();
  assert.equal(current.recognition.waiting, 1);
  assert.equal(current.recognition.running, 1);
  assert.equal(current.artwork.waiting, 1);
  assert.equal(current.artwork.running, 1);
  assert.equal(current.providers["musicbrainz.org"].inFlight, 1);
  assert.equal(current.providers["images.test"].inFlight, 1);
  await control({ advance: 181000, freeBytes: 200_000_000 });
  await sample();
  await control({ advance: 60000 });
  await sample();
  for (const name of ["recognition-queue", "artwork-queue", "disk-free"])
    assert.ok(
      records().some(
        (event) =>
          event.name === name && ["warning", "critical"].includes(event.event),
      ),
      name,
    );
  await control({ failWrites: true, release: "lookup-0" });
  await until(() => output.includes("MONITOR_GATE lookup-1"));
  assert.ok(
    records().some(
      (event) =>
        event.name === "storage-report-write" && event.event === "warning",
    ),
  );
  await control({ failWrites: false, release: "lookup-1" });
  await until(() =>
    records().some(
      (event) =>
        event.name === "storage-report-write" && event.event === "recovered",
    ),
  );
  await control({ release: "cover-2" });
  await until(() => output.includes("MONITOR_GATE cover-3"));
  await control({ release: "cover-3", freeBytes: 8_000_000_000 });
  await until(
    () => (output.match(/artwork retry|lookup complete/g) ?? []).length >= 2,
  );
  current = await sample();
  assert.equal(current.recognition.waiting + current.recognition.running, 0);
  assert.equal(current.artwork.waiting + current.artwork.running, 0);
  assert.deepEqual(current.lookupsSinceLastSummary, {
    matched: 0,
    noMatch: 1,
    failed: 1,
  });
  assert.equal(current.providers["musicbrainz.org"].failures, 1);
  assert.equal(current.providers["images.test"].notFound, 2);
  assert.equal(current.providers["images.test"].failures, 0);
  for (const name of ["recognition-queue", "artwork-queue", "disk-free"])
    assert.ok(
      records().some(
        (event) => event.name === name && event.event === "recovered",
      ),
      name,
    );
  assert.ok(current.cache.bytes > 0);
  assert.deepEqual(current.storage.unresolvedOperations, []);
  assert.ok(
    !JSON.stringify(records()).includes("127.0.0.1"),
    "Monitor logs must not contain caller addresses",
  );
  const exited = once(child, "exit");
  child.kill("SIGTERM");
  assert.equal((await exited)[0], 0, output);
  console.log(
    "PASS: quiet defaults retain alerts and recovery, opt-in queue/provider/storage summaries, storage failure recovery, warning transitions and graceful shutdown.",
  );
} finally {
  if (child.exitCode === null) {
    const exited = once(child, "exit");
    child.kill("SIGKILL");
    await exited;
  }
  await rm(directory, { recursive: true, force: true });
}
