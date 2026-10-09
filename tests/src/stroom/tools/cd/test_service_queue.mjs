import { cd } from "../../../../../tools/contracts/load.mjs";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import dgram from "node:dgram";
import { mkdtemp, rm, readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { saveReport } from "../../../../../tools/cd/cache.mjs";
import { discId } from "../../../../../tools/cd/disc.mjs";
import { assertCover } from "./cover_fixture.mjs";

for (const shutdown of [false, true]) {
  const directory = await mkdtemp(join(tmpdir(), "stroom-queue-"));
  const child = spawn(
    process.execPath,
    [
      "--import",
      fileURLToPath(new URL("./queue_fixture.mjs", import.meta.url)),
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
      env: { ...process.env, QUEUE_BLOCK_RECOGNITION: shutdown ? "1" : "" },
      stdio: ["ignore", "pipe", "pipe", "ipc"],
    },
  );
  const socket = dgram.createSocket("udp4");
  const replies = [];
  let output = "";
  child.stdout.on("data", (data) => (output += data));
  child.stderr.on("data", (data) => (output += data));
  socket.on("message", (packet) =>
    replies.push({
      token: packet.readUInt32BE(4),
      ...(packet.subarray(0, 4).toString() === "CDB1"
        ? { busy: true, retryMs: packet.readUInt32BE(8) }
        : JSON.parse(packet.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES)).data),
    }),
  );
  const discs = [
    { first: 1, offsets: [150, 4650], leadout: 9150 },
    { first: 1, offsets: [150, 4725], leadout: 9300 },
  ];
  async function until(check) {
    const deadline = Date.now() + 5000;
    while (!check()) {
      assert.equal(child.exitCode, null, output);
      assert.ok(Date.now() < deadline, output);
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  }
  function send(index, token, track = 1) {
    socket.send(
      Buffer.from(
        JSON.stringify({
          ...discs[index],
          type: "stroom-cd-toc",
          version: 1,
          generation: index + 1,
          request: token,
          track,
          service: "127.0.0.1",
        }),
      ),
      cd.CD_LOOKUP_PORT,
      "127.0.0.1",
    );
  }
  try {
    await until(() => output.includes("CD recognition listening"));
    send(0, 100);
    await until(() => output.includes("QUEUE_WAIT cover-0"));
    for (let i = 0; i < 10; ++i) send(1, 200);
    if (shutdown) {
      // Shutdown must wait for both workers and must not start queued artwork.
      await until(() => output.includes("QUEUE_WAIT recognition-1"));
      // A full queue returns correlated retry replies instead of growing.
      for (let index = 2; index < 130; ++index) {
        socket.send(
          Buffer.from(
            JSON.stringify({
              first: 1,
              offsets: [150, 4800 + index],
              leadout: 9600 + index,
              type: "stroom-cd-toc",
              version: 1,
              generation: index + 1,
              request: 300 + index,
              track: 1,
              service: "127.0.0.1",
            }),
          ),
          cd.CD_LOOKUP_PORT,
          "127.0.0.1",
        );
        await until(() =>
          index < 128
            ? (output.match(/Received /g) ?? []).length === index + 1
            : replies.some(
                (reply) => reply.token === 300 + index && reply.busy,
              ),
        );
      }
      assert.equal((output.match(/Received /g) ?? []).length, 128);
      assert.ok(
        replies
          .filter((reply) => reply.busy)
          .every((reply) => reply.retryMs === 15000),
      );
      // Cached disk results remain available with all 128 job slots occupied.
      const cachedToc = { first: 1, offsets: [150, 5000], leadout: 10000 };
      const cachedId = discId(cachedToc);
      const coverFile = cachedId + "-cached.jpg";
      const jpeg = Buffer.from([0xff, 0xd8, 0xff, 0xd9]);
      await writeFile(join(directory, coverFile), jpeg);
      await saveReport(directory, cachedId, {
        discId: cachedId,
        toc: cachedToc,
        selected: { releaseId: "cached", mediumPosition: 1 },
        candidates: [
          {
            id: "cached",
            title: "Cached album",
            artist: "Artist",
            coverFile,
            media: [
              {
                position: 1,
                tracks: [{ title: "Cached first" }, { title: "Cached second" }],
              },
            ],
          },
        ],
        artworkRetryable: false,
      });
      socket.send(
        Buffer.from(
          JSON.stringify({
            ...cachedToc,
            type: "stroom-cd-toc",
            version: 1,
            generation: 1000,
            request: 1000,
            track: 2,
            service: "127.0.0.1",
          }),
        ),
        cd.CD_LOOKUP_PORT,
        "127.0.0.1",
      );
      await until(() =>
        replies.some((reply) => reply.token === 1000 && !reply.busy),
      );
      const cachedReply = replies.find(
        (reply) => reply.token === 1000 && !reply.busy,
      );
      assert.equal(cachedReply.title, "Cached second");
      assert.deepEqual(
        Buffer.from(await (await fetch(cachedReply.artwork_url)).arrayBuffer()),
        jpeg,
      );
      assert.equal((output.match(/Received /g) ?? []).length, 128);
      const exited = once(child, "exit");
      child.kill("SIGTERM");
      await new Promise((resolve) => setTimeout(resolve, 50));
      child.send("cover-0");
      await until(() => output.includes("Report:"));
      assert.equal(child.exitCode, null, "Recognition is still active");
      child.send("recognition-1");
      const [code] = await exited;
      assert.equal(code, 0, output);
      assert.ok(!output.includes("QUEUE_WAIT cover-1"), output);
    } else {
      await until(() => replies.some((reply) => reply.token === 200));
      assert.equal(
        replies.find((reply) => reply.token === 200).title,
        "Disc 1 track 1",
      );
      assert.equal(
        replies.find((reply) => reply.token === 200).artwork_url,
        "",
      );
      assert.ok(
        !output.includes("QUEUE_WAIT cover-1"),
        "Artwork remains bounded to one job",
      );
      assert.ok(!output.includes("Report:"), "Disc A cover has not finished");
      send(1, 201, 2);
      await until(() => replies.some((reply) => reply.token === 201));
      assert.equal(
        replies.find((reply) => reply.token === 201).title,
        "Disc 1 track 2",
      );
      child.send("cover-0");
      await until(() => output.includes("QUEUE_WAIT cover-1"));
      child.send("cover-1");
      await until(() => (output.match(/Report:/g) ?? []).length === 2);
      await until(() =>
        replies.some((reply) => reply.token === 201 && reply.artwork_url),
      );
      for (const index of [0, 1]) {
        assert.equal(
          (output.match(new RegExp(`QUEUE_LOOKUP ${index}`, "g")) ?? []).length,
          1,
        );
        const report = JSON.parse(
          await readFile(
            join(directory, `${discId(discs[index])}.json`),
            "utf8",
          ),
        );
        assert.equal(report.candidates[0].title, `Album ${index}`);
        assert.ok(report.candidates[0].coverFile);
      }
      assert.ok(
        replies
          .filter((reply) => reply.token >= 200)
          .every((reply) => reply.album === "Album 1"),
      );
      const latest = replies.findLast(
        (reply) => reply.token === 201 && reply.artwork_url,
      );
      assert.equal(latest.title, "Disc 1 track 2");
      assertCover(await (await fetch(latest.artwork_url)).arrayBuffer());
      const exited = once(child, "exit");
      child.kill("SIGTERM");
      const [code] = await exited;
      assert.equal(code, 0, output);
    }
  } finally {
    child.kill("SIGKILL");
    socket.close();
    await rm(directory, { recursive: true, force: true });
  }
}
console.log(
  "PASS: uncached recognition bypasses blocked artwork, deduplication, track tokens, persistence and concurrent shutdown",
);
