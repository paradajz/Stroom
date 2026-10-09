import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import dgram from "node:dgram";
import { mkdtemp, writeFile, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { cd } from "../../../../../tools/contracts/load.mjs";
import { discId } from "../../../../../tools/cd/disc.mjs";
import { saveReport } from "../../../../../tools/cd/cache.mjs";

const directory = await mkdtemp("/tmp/stroom-cache-eviction-");
// Force recognition failures without contacting any external providers.
await writeFile(
  directory + "/provider-cooldowns.json",
  JSON.stringify({
    "musicbrainz.org": Date.now() + 600000,
    "db.cue.tools": Date.now() + 600000,
    "gnudb.gnudb.org": Date.now() + 600000,
  }),
);
const toc = (index) => ({
  first: 1,
  offsets: [150, 15000 + index],
  leadout: 32000 + index,
});
const cachedToc = toc(129);
await saveReport(directory, discId(cachedToc), {
  discId: discId(cachedToc),
  toc: cachedToc,
  selected: { releaseId: "cached", mediumPosition: 1 },
  candidates: [
    {
      id: "cached",
      title: "Album",
      artist: "Artist",
      media: [
        {
          position: 1,
          tracks: [{ title: "First track" }, { title: "Second track" }],
        },
      ],
    },
  ],
  artworkRetryable: false,
});
const child = spawn(
  process.execPath,
  [
    fileURLToPath(
      new URL("../../../../../tools/cd/service.mjs", import.meta.url),
    ),
    "--contact",
    "test@example.test",
    "--ps2",
    "127.0.0.1",
    "--out",
    directory,
  ],
  { stdio: ["ignore", "pipe", "pipe"] },
);
const socket = dgram.createSocket("udp4");
let output = "";
const replies = [];
child.stdout.on("data", (data) => (output += data));
child.stderr.on("data", (data) => (output += data));
socket.on("message", (packet) =>
  replies.push(
    JSON.parse(packet.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES)).data,
  ),
);
const receivedCount = () => (output.match(/Received /g) ?? []).length;
async function until(check) {
  const deadline = Date.now() + 5000;
  while (!check()) {
    assert.equal(child.exitCode, null, output);
    assert.ok(Date.now() < deadline, output);
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
}
function send(index) {
  socket.send(
    Buffer.from(
      JSON.stringify({
        ...toc(index),
        type: "stroom-cd-toc",
        version: 1,
        generation: index,
        request: index,
        track: 1,
        service: "127.0.0.1",
      }),
    ),
    cd.CD_LOOKUP_PORT,
    "127.0.0.1",
  );
}
try {
  await until(() => output.includes("CD recognition listening"));
  for (let index = 1; index <= 128; index++) {
    send(index);
    await until(() => (output.match(/lookup failed:/g) ?? []).length === index);
  }
  send(129);
  await until(() => replies.length > 0);
  assert.equal(
    receivedCount(),
    129,
    "A full cache of failed jobs must admit a new disc",
  );
  assert.equal(
    replies[0].title,
    "First track",
    "New discs still receive cached metadata",
  );
  send(129);
  await until(() => replies.length === 2);
  assert.equal(
    receivedCount(),
    129,
    "Repeated probes reuse the retained entry",
  );
  send(1);
  await until(() => receivedCount() === 130);
  assert.match(
    output,
    new RegExp(
      "Received .*disc ID " +
        discId(toc(1)).replace(/[.*+?^${}()|[\]\\]/g, "\\$&"),
    ),
  );
  console.log(
    "PASS: idle failed entries are evicted and new discs receive metadata without restart.",
  );
} finally {
  socket.close();
  if (child.exitCode === null) {
    const exited = once(child, "exit");
    child.kill("SIGTERM");
    await exited;
  }
  await rm(directory, { recursive: true, force: true });
}
