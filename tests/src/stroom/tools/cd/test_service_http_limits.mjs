import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import net from "node:net";
import http from "node:http";
import { Service, serviceConfig } from "./http_service_fixture.mjs";
import { mkdtemp, writeFile, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { cd } from "../../../../../tools/contracts/load.mjs";
import { discId } from "../../../../../tools/cd/recognition/disc.mjs";
import { saveReport } from "../../../../../tools/cd/recognition/cache.mjs";
const directory = await mkdtemp("/tmp/stroom-http-limits-");
const toc = { first: 1, offsets: [150], leadout: 15000 },
  id = discId(toc),
  name = id + "-cover.jpg";
await writeFile(directory + "/" + name, Buffer.from([0xff, 0xd8, 0xff, 0xd9]));
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
    await serviceConfig(directory, { log_statistics: true }),
  ],
  { stdio: ["ignore", "pipe", "pipe", "ipc"] },
);
let output = "";
child.stdout.on("data", (data) => (output += data));
child.stderr.on("data", (data) => (output += data));
const sockets = [];
async function until(check) {
  const end = Date.now() + 5000;
  while (!(await check())) {
    assert.equal(child.exitCode, null, output);
    assert.ok(Date.now() < end, output);
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
}
async function control(message = {}) {
  const result = once(child, "message");
  child.send(message);
  return (await result)[0];
}
async function connection(ip) {
  const socket = net.connect({
    port: cd.CD_LOOKUP_PORT,
    host: "127.0.0.1",
    localAddress: ip,
  });
  sockets.push(socket);
  socket.on("error", () => {});
  socket.resume();
  await once(socket, "connect");
  return socket;
}
function get(ip, path = "/missing", options = {}) {
  return new Promise((resolve, reject) => {
    const request = http.request(
      {
        hostname: "127.0.0.1",
        port: cd.CD_LOOKUP_PORT,
        localAddress: ip,
        path,
        agent: false,
        headers: { Connection: "keep-alive", ...options.headers },
        method: options.method ?? "GET",
      },
      (response) => {
        response.resume();
        response.on("error", reject);
        response.on("end", () =>
          resolve({ status: response.statusCode, headers: response.headers }),
        );
      },
    );
    request.on("error", reject);
    request.end();
  });
}
const summaries = () =>
  output
    .split("\n")
    .filter((line) => line.startsWith("CD monitor: "))
    .flatMap((line) => {
      try {
        const entry = JSON.parse(line.slice(12));
        return entry.event === "summary" ? [entry] : [];
      } catch {
        return [];
      }
    });
async function sample() {
  const before = summaries().length;
  await control({ sample: true });
  await until(() => summaries().length > before);
  return summaries().at(-1).http;
}
try {
  await until(() => output.includes("CD recognition listening"));
  // The native TCP cap covers sockets that send no HTTP bytes at all.
  const held = [];
  for (let ip = 2; ip < 10; ++ip)
    for (let count = 0; count < 8; ++count)
      held.push(await connection("127.0.0." + ip));
  await until(async () => (await control()).connections === 64);
  const over = await connection("127.0.0.10");
  await until(() => over.destroyed);
  let summary = await sample();
  assert.equal(summary.connections, 64);
  assert.equal(summary.activeCovers, 0);
  assert.equal(summary.rejected.connectionsGlobal, 1);
  for (const socket of held) socket.destroy();
  await until(async () => (await control()).connections === 0);
  const same = [];
  for (let i = 0; i < 8; ++i) same.push(await connection("127.0.0.11"));
  const ninth = await connection("127.0.0.11");
  await until(() => ninth.destroyed);
  assert.equal((await sample()).rejected.connectionsIp, 1);
  // Sending partial headers does not extend the deadline from TCP acceptance.
  same[0].write("GET / HTTP/1.1\r\nHost:");
  await control({ expire: "headers" });
  await until(async () => (await control()).connections === 0);
  assert.equal((await sample()).rejected.headersTimeout, 8);
  // Every request consumes the budget, even missing paths and different sockets.
  await control({ advance: 6000 });
  const before = (await control()).reads;
  for (let i = 0; i < 10; ++i) {
    const result = await get("127.0.0.12");
    assert.equal(result.status, 404);
    assert.equal(result.headers.connection, "close");
  }
  let result = await get("127.0.0.12");
  assert.equal(result.status, 429);
  assert.equal(result.headers["retry-after"], "1");
  assert.equal((await control()).reads, before);
  await control({ advance: 6000 });
  assert.equal((await get("127.0.0.12", "/covers/" + name)).status, 200);
  // Transfer slots remain held through aborted file reads, separately per IP.
  await control({ gateNext: true });
  const first = get("127.0.0.13", "/covers/" + name);
  await until(async () => (await control()).gatedReads === 1);
  const reads = (await control()).reads;
  assert.equal((await get("127.0.0.13", "/covers/" + name)).status, 503);
  assert.equal((await control()).reads, reads);
  await control({ gateNext: true });
  const second = get("127.0.0.14", "/covers/" + name);
  await until(async () => (await control()).gatedReads === 2);
  assert.equal((await get("127.0.0.18", "/covers/" + name)).status, 200);
  summary = await sample();
  assert.equal(summary.activeCovers, 2);
  assert.equal(summary.rejected.coversIp, 1);
  await control({ releaseReads: true });
  assert.equal((await first).status, 200);
  assert.equal((await second).status, 200);
  assert.equal((await get("127.0.0.13", "/covers/" + name)).status, 200);
  // Requests with bodies and alternate HTTP event paths cannot read a cover.
  const beforeUnsupported = (await control()).reads;
  assert.equal(
    (
      await get("127.0.0.15", "/covers/" + name, {
        headers: { "Content-Length": "1", Expect: "100-continue" },
      })
    ).status,
    400,
  );
  for (const text of [
    "CONNECT example.test:443 HTTP/1.1\r\nHost: example.test\r\n\r\n",
    "GET / HTTP/1.1\r\nHost: test\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n",
    "garbage\r\n\r\n",
  ]) {
    const socket = await connection("127.0.0.16");
    socket.write(text);
    await until(() => socket.destroyed);
  }
  const pipeline = await connection("127.0.0.17");
  pipeline.write("GET /missing HTTP/1.1\r\nHost: test\r\n\r\n".repeat(100));
  await until(() => pipeline.destroyed);
  assert.equal((await control()).reads, beforeUnsupported);
  summary = await sample();
  assert.ok(summary.rejected.unsupported >= 3);
  assert.ok(summary.rejected.malformed >= 1);
  assert.ok(summary.rejected.pipelined >= 1);
  // Saturate the global request allowance with distinct callers.
  await control({ advance: 6000 });
  let globalRejections = 0;
  for (let i = 30; i < 90; ++i)
    if ((await get("127.0.0." + i)).status === 503) ++globalRejections;
  assert.ok(globalRejections > 0);
  summary = await sample();
  assert.ok(summary.rejectedSinceLastSummary.requestsGlobal > 0);
  assert.equal((await control()).providerCalls, 0);
  await until(async () => (await control()).connections === 0);
  summary = await sample();
  assert.equal(summary.connections, 0);
  assert.equal(summary.activeCovers, 0);
  assert.equal(summary.rejectedSinceLastSummary.requestsGlobal, 0);
  const exited = once(child, "exit");
  child.kill("SIGTERM");
  assert.equal((await exited)[0], 0, output);
  console.log(
    "PASS: real silent/partial TCP connections, global/per-IP caps, HTTP refill across reconnects, per-IP cover slots, unsupported/pipelined requests, metadata independence and aggregate monitoring.",
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
