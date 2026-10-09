import assert from "node:assert/strict";
import dgram from "node:dgram";
import { spawn } from "node:child_process";
import { mkdtemp, readdir, readFile, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const client = process.argv[2];
const directory = await mkdtemp(
  path.join(os.tmpdir(), "stroom-diagnostic-test-"),
);
const socket = dgram.createSocket("udp4");
let mode = "success";
let dropped = false;
let cleared = false;
let previousPage;
const records = Array.from({ length: 9 }, (_, i) => [
  100 + i,
  2,
  3840,
  0,
  1,
  4096,
  i * 3840,
  7,
]);
records[1] = [101, 108, 1, 1, 0, 0, 0, 0];
records[2] = [102, 110, 42, 99, 1, 1, 32, 0];
records[3] = [103, 109, 42, 99, 0, 200, 300, 0];
records[4] = [104, 9, 2, 150, 12, 1024, 512, 0x101];
socket.on("message", (data, peer) => {
  const query = data.toString();
  const send = (reply) =>
    socket.send(
      Buffer.from(JSON.stringify({ diagnosticVersion: 1, ...reply })),
      peer.port,
      peer.address,
    );
  if (query === "STROOM_DIAGNOSTIC_CLEAR") {
    cleared = true;
    send({ cleared: true });
  } else if (query === "STROOM_DIAGNOSTIC") {
    send({
      missed: 0,
      iopStatus: 1,
      iopLost: 0,
      sackKnown: 1,
      sackEnabled: 1,
      sackNegotiated: 1,
      events: [
        {
          id: 42,
          count: 9,
          complete: 1,
          trigger: 105,
          session: 7,
          truncated: 0,
        },
      ],
    });
  } else {
    const [, id, offset] = query.split(" ").map((v, i) => (i ? Number(v) : v));
    assert.equal(id, 42);
    if (mode === "unavailable") {
      send({ id, offset, unavailable: true });
    } else if (!dropped) {
      dropped = true;
    } else {
      // An old page arrives before the requested page. The client must ignore it.
      if (previousPage) send(previousPage);
      previousPage = { id, offset, records: records.slice(offset, offset + 8) };
      send(previousPage);
    }
  }
});
await new Promise((resolve) => socket.bind(12888, "127.0.0.1", resolve));

async function run(action) {
  return new Promise((resolve, reject) => {
    const args = [client, "127.0.0.1", directory];
    if (action) args.push(action);
    const child = spawn(process.execPath, args, {
      stdio: ["ignore", "pipe", "pipe"],
    });
    let output = "";
    child.stdout.on("data", (data) => (output += data));
    child.stderr.on("data", (data) => (output += data));
    child.on("error", reject);
    child.on("close", (code) => resolve({ code, output }));
  });
}

try {
  const success = await run();
  assert.equal(success.code, 0, success.output);
  let files = await readdir(directory);
  assert.equal(files.length, 1);
  const result = JSON.parse(
    await readFile(path.join(directory, files[0]), "utf8"),
  );
  assert.deepEqual(result.events[0].records, records);
  assert.equal(result.sackNegotiated, 1);
  assert.equal(result.events[0].decoded[1].kind, "sackState");
  assert.equal(result.events[0].decoded[2].blockCount, 1);
  assert.equal(result.events[0].decoded[3].left, 200);
  assert.equal(result.events[0].decoded[3].right, 300);
  assert.equal(result.events[0].decoded[0].relativeMs, -5);
  const cover = result.events[0].decoded[4];
  assert.equal(cover.kind, "cover");
  assert.equal(cover.phase, 2);
  assert.equal(cover.phaseName, "decode-end");
  assert.equal(cover.beginRelativeMs, -151);
  assert.equal(cover.durationMs, 150);
  assert.equal(cover.metadataRevision, 12);
  assert.equal(cover.value0, 1024);
  assert.equal(cover.value1, 512);
  assert.equal(cover.value2, 0x101);
  mode = "unavailable";
  const failure = await run();
  assert.notEqual(failure.code, 0);
  assert.match(failure.output, /cleared or console restarted/);
  assert.deepEqual(await readdir(directory), files);
  assert.equal((await run("clear")).code, 0);
  assert.equal(cleared, true);
} finally {
  socket.close();
  await rm(directory, { recursive: true, force: true });
}
