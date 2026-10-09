import { cd } from "../../../../../tools/contracts/load.mjs";
import { assertCover } from "./cover_fixture.mjs";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import dgram from "node:dgram";
import { mkdtemp, readdir, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { metadataReply } from "../../../../../tools/cd/response.mjs";
const directory = await mkdtemp(join(tmpdir(), "stroom-cd-udp-"));
const child = spawn(
  process.execPath,
  [
    "--import",
    fileURLToPath(new URL("./service_fixture.mjs", import.meta.url)),
    fileURLToPath(
      new URL("../../../../../tools/cd/service.mjs", import.meta.url),
    ),
    "--contact",
    "test@example.test",
    "--out",
    directory,
  ],
  { stdio: ["ignore", "pipe", "pipe", "ipc"] },
);
const socket = dgram.createSocket("udp4");
const movedSocket = dgram.createSocket("udp4");
const replies = [];
socket.on("message", (data) => replies.push(data));
let output = "";
child.stdout.on("data", (chunk) => (output += chunk));
child.stderr.on("data", (chunk) => (output += chunk));
async function waitFor(text) {
  const deadline = Date.now() + 5000;
  while (!output.includes(text)) {
    assert.equal(child.exitCode, null, output);
    assert.ok(Date.now() < deadline, output);
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
}
try {
  await waitFor("CD recognition listening");
  assert.ok(output.includes("accepting any console address"), output);
  socket.send(Buffer.from("bad JSON"), cd.CD_LOOKUP_PORT, "127.0.0.1");
  await waitFor("Ignored invalid TOC");
  const toc = Buffer.from(
    JSON.stringify({
      type: "stroom-cd-toc",
      version: 1,
      generation: 1,
      first: 1,
      request: 123,
      track: 1,
      service: "127.0.0.1",
      leadout: 32314,
      offsets: [150, 15363],
    }),
  );
  for (let i = 0; i < 10; ++i) socket.send(toc, cd.CD_LOOKUP_PORT, "127.0.0.1");
  await waitFor("FIXTURE_COVER_WAIT");
  assert.ok(replies.length > 0, "Labels must arrive while artwork is blocked");
  const initial = JSON.parse(
    replies[0].subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES),
  );
  assert.equal(initial.data.title, "First track");
  assert.equal(initial.data.artwork_url, "");
  assert.ok(!output.includes("Report:"), "Artwork lookup is still in progress");
  const during = { ...JSON.parse(toc), track: 2, request: 456 };
  const updated = new Promise((resolve) => {
    const handler = (packet) => {
      if (packet.readUInt32BE(4) === 456) {
        socket.off("message", handler);
        resolve(packet);
      }
    };
    socket.on("message", handler);
  });
  socket.send(
    Buffer.from(JSON.stringify(during)),
    cd.CD_LOOKUP_PORT,
    "127.0.0.1",
  );
  const earlyTrack = JSON.parse(
    (await updated).subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES),
  );
  assert.equal(earlyTrack.data.title, "Second track");
  assert.equal(earlyTrack.data.artwork_url, "");
  child.send("release-cover");
  await waitFor("Report:");
  await new Promise((resolve) => setTimeout(resolve, 100));
  assert.equal((output.match(/FIXTURE_REQUEST/g) ?? []).length, 2);
  const files = await readdir(directory);
  assert.equal(files.length, 2);
  const report = JSON.parse(
    await readFile(
      join(
        directory,
        files.find((name) => name.endsWith(".json")),
      ),
      "utf8",
    ),
  );
  assert.equal(report.candidates[0].title, "Fixture album");
  assert.equal(report.match, "exact");
  assert.ok(replies.length > 0, "Service must return selected metadata");
  let reply = replies.at(-1);
  // Independent wire fixture: CDM1, token 456, artwork available.
  assert.deepEqual(
    reply.subarray(0, 9),
    Buffer.from([67, 68, 77, 49, 0, 0, 1, 200, 1]),
  );
  assert.equal(reply.readUInt32BE(4), 456);
  let metadata = JSON.parse(reply.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES));
  assert.equal(metadata.type, "metadata");
  assert.equal(metadata.data.title, "Second track");
  assert.equal(metadata.data.artist, "Fixture artist");
  assert.equal(metadata.data.album, "Fixture album");
  const cover = await fetch(metadata.data.artwork_url);
  assert.equal(cover.status, 200);
  assert.equal(cover.headers.get("content-type"), "image/jpeg");
  await assertCover(await cover.arrayBuffer());
  assert.equal(
    (await fetch(`http://127.0.0.1:${cd.CD_LOOKUP_PORT}/covers/unknown.jpg`))
      .status,
    404,
  );
  const changed = { ...JSON.parse(toc), generation: 2, track: 1, request: 789 };
  movedSocket.bind(0, "127.0.0.2");
  await once(movedSocket, "listening");
  const received = once(movedSocket, "message");
  movedSocket.send(
    Buffer.from(JSON.stringify(changed)),
    cd.CD_LOOKUP_PORT,
    "127.0.0.1",
  );
  [reply] = await received;
  assert.equal(reply.readUInt32BE(4), 789);
  metadata = JSON.parse(reply.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES));
  assert.equal(metadata.data.title, "First track");
  assert.equal(
    (output.match(/FIXTURE_REQUEST/g) ?? []).length,
    2,
    "Track change reuses lookup and cover",
  );
  assert.equal(
    metadataReply({ ...report, selected: null }, changed, cd.CD_LOOKUP_PORT),
    null,
  );
  assert.equal(
    metadataReply(report, { ...changed, track: 99 }, cd.CD_LOOKUP_PORT),
    null,
  );
  const longReport = structuredClone(report);
  longReport.candidates[0].media[0].tracks[0].title = "é".repeat(100) + "\n";
  const bounded = JSON.parse(
    metadataReply(longReport, changed, cd.CD_LOOKUP_PORT).subarray(
      cd.CD_LOOKUP_REPLY_HEADER_BYTES,
    ),
  );
  assert.equal(Buffer.byteLength(bounded.data.title), 126);
  assert.ok(!bounded.data.title.includes("�"));
  const exited = once(child, "exit");
  child.kill("SIGTERM");
  await exited;
  console.log(
    "PASS: default address acceptance, reply routing after an address change, early labels, track changes during artwork lookup, cover update, live UDP ingestion, malformed message isolation, repeated-disc deduplication, report persistence and shutdown.",
  );
} finally {
  child.kill("SIGKILL");
  socket.close();
  movedSocket.close();
  await rm(directory, { recursive: true, force: true });
}
