import { cd } from "../../../../../tools/contracts/load.mjs";
import assert from "node:assert/strict";
import { mkdtemp, rm, writeFile, readFile, unlink } from "node:fs/promises";
import { spawn } from "node:child_process";
import { once } from "node:events";
import dgram from "node:dgram";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";
import {
  cacheDirectory,
  cachedReport,
  saveReport,
} from "../../../../../tools/cd/cache.mjs";
import { discId } from "../../../../../tools/cd/disc.mjs";
import { prepareCover } from "../../../../../tools/cd/image.mjs";
import { coverInput, assertCover } from "./cover_fixture.mjs";

assert.equal(
  cacheDirectory("linux", "/home/user", ""),
  "/home/user/.cache/stroom/cd",
);
assert.equal(
  cacheDirectory("darwin", "/Users/user", ""),
  "/Users/user/Library/Caches/stroom/cd",
);
assert.equal(
  cacheDirectory("darwin", "/Users/user", "/custom"),
  "/custom/stroom/cd",
);
assert.equal(
  cacheDirectory("linux", "/home/user", "relative"),
  "/home/user/.cache/stroom/cd",
);
const directory = await mkdtemp("/tmp/stroom-cache-");
const toc = { first: 1, offsets: [150, 15363], leadout: 32314 };
const id = discId(toc);
const coverFile = id + "-release.jpg";
const report = {
  discId: id,
  toc,
  selected: { releaseId: "release", mediumPosition: 1 },
  artworkRetryable: false,
  candidates: [
    {
      id: "release",
      title: "Cached album",
      artist: "Cached artist",
      coverFile,
      coverUrl: "https://example.test/cover.jpg",
      media: [
        {
          position: 1,
          tracks: [{ title: "First track" }, { title: "Second track" }],
        },
      ],
    },
  ],
};
let child;
let blocker;
const socket = dgram.createSocket("udp4");
try {
  assert.equal(await cachedReport(directory, id), null);
  await writeFile(directory + "/" + coverFile, await prepareCover(coverInput));
  await saveReport(directory, id, report);
  assert.equal(
    (await cachedReport(directory, id)).candidates[0].coverFile,
    coverFile,
  );

  // Cached labels survive a port conflict; a later restart serves the same cover.
  for (let run = 0; run < 3; run++) {
    if (run === 1) {
      blocker = createServer((_req, res) => res.writeHead(404).end());
      blocker.listen(cd.CD_LOOKUP_PORT, "0.0.0.0");
      await once(blocker, "listening");
    }
    let output = "";
    child = spawn(
      process.execPath,
      [
        "--import",
        fileURLToPath(new URL("./service_fixture.mjs", import.meta.url)),
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
      {
        env: { ...process.env, CACHE_OFFLINE: "1" },
        stdio: ["ignore", "pipe", "pipe"],
      },
    );
    child.stdout.on("data", (data) => (output += data));
    child.stderr.on("data", (data) => (output += data));
    const deadline = Date.now() + 5000;
    while (!output.includes("CD recognition listening")) {
      assert.equal(child.exitCode, null, output);
      assert.ok(Date.now() < deadline, output);
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
    const received = once(socket, "message");
    socket.send(
      Buffer.from(
        JSON.stringify({
          ...toc,
          type: "stroom-cd-toc",
          version: 1,
          generation: run + 1,
          request: run + 1,
          track: (run % 2) + 1,
          service: "127.0.0.1",
        }),
      ),
      cd.CD_LOOKUP_PORT,
      "127.0.0.1",
    );
    const [packet] = await received;
    const metadata = JSON.parse(
      packet.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES),
    ).data;
    assert.equal(metadata.title, run % 2 ? "Second track" : "First track");
    assert.equal(metadata.artist, "Cached artist");
    assert.equal(metadata.album, "Cached album");
    if (run === 1) {
      assert.match(output, /Cover HTTP unavailable:.*EADDRINUSE/);
      assert.equal(metadata.artwork_url, "");
      assert.equal(
        packet[cd.CD_LOOKUP_ARTWORK_STATE_OFFSET],
        cd.CD_LOOKUP_ARTWORK_UNAVAILABLE,
      );
      assert.equal(
        (await cachedReport(directory, id)).candidates[0].coverFile,
        coverFile,
      );
    } else {
      assert.equal(
        packet[cd.CD_LOOKUP_ARTWORK_STATE_OFFSET],
        cd.CD_LOOKUP_ARTWORK_AVAILABLE,
      );
      await assertCover(
        await (await fetch(metadata.artwork_url)).arrayBuffer(),
      );
    }
    assert.ok(!output.includes("FIXTURE_REQUEST"), output);
    const exited = once(child, "exit");
    child.kill("SIGTERM");
    await exited;
    child = undefined;
    if (blocker) {
      await new Promise((resolve) => blocker.close(resolve));
      blocker = undefined;
    }
  }

  // A cached disc must bypass another disc's unfinished provider request.
  let output = "";
  child = spawn(
    process.execPath,
    [
      "--import",
      fileURLToPath(new URL("./service_fixture.mjs", import.meta.url)),
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
    { stdio: ["ignore", "pipe", "pipe", "ipc"] },
  );
  child.stdout.on("data", (data) => (output += data));
  child.stderr.on("data", (data) => (output += data));
  async function waitFor(condition) {
    const deadline = Date.now() + 5000;
    while (!condition()) {
      assert.equal(child.exitCode, null, output);
      assert.ok(Date.now() < deadline, output);
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  }
  function send(layout, request, track) {
    socket.send(
      Buffer.from(
        JSON.stringify({
          ...layout,
          type: "stroom-cd-toc",
          version: 1,
          generation: 1,
          request,
          track,
          service: "127.0.0.1",
        }),
      ),
      cd.CD_LOOKUP_PORT,
      "127.0.0.1",
    );
  }
  await waitFor(() => output.includes("CD recognition listening"));
  send({ ...toc, leadout: toc.leadout + 1 }, 100, 1);
  await waitFor(() => output.includes("FIXTURE_COVER_WAIT"));
  const replies = [];
  const receive = (packet) => replies.push(packet);
  socket.on("message", receive);
  try {
    send(toc, 200, 1);
    send(toc, 201, 2);
    await waitFor(() =>
      replies.some((packet) => packet.readUInt32BE(4) === 201),
    );
    const packet = replies.find((packet) => packet.readUInt32BE(4) === 201);
    const metadata = JSON.parse(
      packet.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES),
    ).data;
    assert.equal(metadata.title, "Second track");
    await assertCover(await (await fetch(metadata.artwork_url)).arrayBuffer());
    assert.ok(
      !output.includes("Report:"),
      "Previous artwork must still be blocked",
    );
    assert.equal((output.match(/FIXTURE_REQUEST/g) ?? []).length, 2);
    assert.equal((output.match(/cache hit/g) ?? []).length, 1);
    child.send("release-cover");
    await waitFor(() => output.includes("Report:"));
  } finally {
    socket.off("message", receive);
  }
  const exited = once(child, "exit");
  child.kill("SIGTERM");
  await exited;
  child = undefined;

  await unlink(directory + "/" + coverFile);
  const missing = await cachedReport(directory, id);
  assert.equal(missing.candidates[0].title, "Cached album");
  assert.equal(missing.candidates[0].coverFile, undefined);
  assert.equal(missing.artworkRetryable, true);
  await writeFile(directory + "/" + coverFile, "broken jpeg");
  assert.equal((await cachedReport(directory, id)).artworkRetryable, true);
  const original = await readFile(directory + "/" + id + ".json", "utf8");
  await writeFile(directory + "/" + id + ".json", "{broken");
  assert.equal(await cachedReport(directory, id), null);
  await writeFile(
    directory + "/" + id + ".json",
    JSON.stringify({ ...JSON.parse(original), cacheVersion: -1 }),
  );
  assert.equal(await cachedReport(directory, id), null);
  await saveReport(directory, id, { ...report, selected: null });
  assert.equal(await cachedReport(directory, id), null);
  console.log(
    "PASS: Linux/macOS cache paths, offline process restarts, current-track replies, cache hits during blocked artwork, JPEG serving and damaged-cache recovery.",
  );
} finally {
  child?.kill("SIGKILL");
  if (blocker) await new Promise((resolve) => blocker.close(resolve));
  socket.close();
  await rm(directory, { recursive: true, force: true });
}
