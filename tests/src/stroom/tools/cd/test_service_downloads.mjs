import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { Service, serviceConfig } from "./http_service_fixture.mjs";
import http from "node:http";
import net from "node:net";
import { mkdtemp, writeFile, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { cd } from "../../../../../tools/contracts/load.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";

const directory = await mkdtemp("/tmp/stroom-downloads-");
const toc = { first: 1, offsets: [150], leadout: 15000 };
const id = discId(toc);
const name = id + "-cover.jpg";
const bytes = Buffer.alloc(cd.CD_LOOKUP_COVER_BYTES);
await writeFile(directory + "/" + name, bytes);
await writeFile(
  directory + "/" + id + "-oversized.jpg",
  Buffer.alloc(bytes.length + 1),
);
await saveReport(directory, id, {
  discId: id,
  toc,
  selected: { releaseId: "cached", mediumPosition: 1 },
  artworkRetryable: false,
  candidates: [
    {
      id: "cached",
      artist: "Artist",
      title: "Album",
      media: [{ position: 1, tracks: [{ title: "Cached labels" }] }],
    },
  ],
});
const child = spawn(
  process.execPath,
  [
    "--import",
    fileURLToPath(new URL("./download_fixture.mjs", import.meta.url)),
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
let output = "";
child.stdout.on("data", (data) => (output += data));
child.stderr.on("data", (data) => (output += data));
const sockets = [];
const api = new Service();
api.peer = 200;
const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(check) {
  const deadline = Date.now() + 5000;
  while (!(await check())) {
    assert.equal(child.exitCode, null, output);
    assert.ok(Date.now() < deadline, output);
    await delay(5);
  }
}
async function control(message = {}) {
  const reply = once(child, "message");
  child.send(message);
  return (await reply)[0];
}
let peer = 1;
async function download(filename = name, method = "GET") {
  await control({ advance: 6000 });
  return new Promise((resolve, reject) => {
    const request = http.request(
      {
        hostname: "127.0.0.1",
        port: cd.CD_LOOKUP_PORT,
        path: "/covers/" + filename,
        method,
        agent: false,
        localAddress: "127.0.0." + ++peer,
      },
      (response) => {
        const chunks = [];
        response.on("data", (chunk) => chunks.push(chunk));
        response.on("error", reject);
        response.on("end", () =>
          resolve({
            status: response.statusCode,
            headers: response.headers,
            bytes: Buffer.concat(chunks),
          }),
        );
      },
    );
    request.on("error", reject);
    request.end();
  });
}
async function connection() {
  const socket = net.connect({
    port: cd.CD_LOOKUP_PORT,
    host: "127.0.0.1",
    localAddress: "127.0.0." + ++peer,
  });
  sockets.push(socket);
  socket.on("error", () => {});
  socket.pause();
  await once(socket, "connect");
  return socket;
}
function send(socket) {
  socket.write(
    `GET /covers/${name} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n`,
  );
}
async function slowReader() {
  const before = await control();
  await control({ slowNext: true });
  const socket = await connection();
  await control({ advance: 6000 });
  send(socket);
  await delay(25);
  const state = await control();
  assert.equal(
    state.images,
    before.images + 1,
    "A paused receiver keeps its transfer slot",
  );
  return socket;
}
async function busy() {
  const before = await control();
  const result = await download();
  assert.equal(result.status, 503);
  assert.equal(result.headers["retry-after"], "15");
  assert.equal(result.headers.connection, "close");
  assert.equal(JSON.parse(result.bytes).error.code, "busy");
  assert.equal(
    (await control()).reads,
    before.reads,
    "Busy downloads must not read another image",
  );
}
try {
  await until(() => output.includes("CD recognition listening"));
  for (let i = 0; i < 12; ++i) {
    const result = await download();
    assert.equal(result.status, 200);
    assert.deepEqual(result.bytes, bytes);
  }
  for (const filename of ["missing.jpg", id + "-oversized.jpg"])
    assert.equal((await download(filename)).status, 404);
  assert.equal((await download(name, "POST")).status, 405);
  await control({ failNext: true });
  assert.equal((await download()).status, 404);
  assert.equal((await download()).status, 200);
  await until(async () => (await control()).deadlines === 0);

  const slow = [];
  for (let i = 0; i < 7; ++i) slow.push(await slowReader());
  await control({ gateNext: true });
  const disconnected = await connection();
  send(disconnected);
  await until(async () => (await control()).gatedReads === 1);
  await busy();
  disconnected.destroy();
  await until(async () => (await control()).openResponses === 7);
  await busy(); // The aborted filesystem operation still owns its slot.
  await control({ releaseReads: true });
  await until(async () => (await control()).activeReads === 0);
  assert.equal((await download()).status, 200);
  assert.ok(
    !output.includes('"code":"ABORT_ERR"'),
    "Disconnects are not storage errors",
  );
  slow.push(await slowReader());
  let state = await control();
  assert.equal(state.images, 8);
  assert.equal(state.deadlines, 8);
  assert.ok(state.queuedBytes <= 8 * (cd.CD_LOOKUP_COVER_BYTES + 1024));
  await busy();

  // The separate metadata path stays available while all download slots are busy.
  const result = await api.post(toc);
  assert.equal(result.status, 200);
  assert.equal(result.body.tracks[0].title, "Cached labels");

  slow[0].destroy();
  await until(async () => (await control()).images === 7);
  await slowReader();
  await busy();
  slow[1].resume();
  await control({ releaseWrites: slow[1].localAddress });
  await until(async () => (await control()).images === 7);
  await slowReader();
  await busy();

  state = await control({ expire: true });
  assert.equal(
    state.expired,
    8,
    "Every unfinished cover has a 30-second total deadline",
  );
  await until(async () => (await control()).images === 0);
  assert.equal((await download()).status, 200);
  for (let i = 0; i < 8; ++i) await slowReader();
  await busy();
  assert.equal((await control()).providerCalls, 0);
  const exited = once(child, "exit");
  child.kill("SIGTERM");
  assert.equal((await exited)[0], 0, output);
  console.log(
    "PASS: eight unfinished cover transfers, bounded response buffers, busy replies without reads, completion/disconnect/deadline cleanup, aborted reads, cached metadata access and shutdown.",
  );
} finally {
  for (const socket of sockets) socket.destroy();
  if (child.exitCode === null) {
    const exited = once(child, "exit");
    child.kill("SIGKILL");
    await exited;
  }
  await rm(directory, { recursive: true, force: true });
}
