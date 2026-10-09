import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import dgram from "node:dgram";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { cd } from "../../../../../tools/contracts/load.mjs";
import { discId } from "../../../../../tools/cd/disc.mjs";
import { saveReport } from "../../../../../tools/cd/cache.mjs";

const toc = { first: 1, offsets: [150, 15363], leadout: 32314 };
const id = discId(toc);
for (const provider of [
  "unfinished",
  "expired",
  "temporary",
  "image",
  "discogs",
]) {
  const directory = await mkdtemp("/tmp/stroom-artwork-restart-");
  const socket = dgram.createSocket("udp4");
  let child,
    output = "",
    replies = [];
  socket.on("message", (packet) =>
    replies.push(
      JSON.parse(packet.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES)).data,
    ),
  );
  const report = {
    discId: id,
    toc,
    selected: { releaseId: "release", mediumPosition: 1 },
    artworkRetryable: true,
    ...(provider === "expired" ? { artworkRetryAt: Date.now() - 1000 } : {}),
    candidates: [
      {
        id: "release",
        title: "Album",
        artist: "Artist",
        coverUrl: "https://coverartarchive.org/release/test/front-250",
        media: [
          {
            position: 1,
            tracks: [{ title: "First track" }, { title: "Second track" }],
          },
        ],
      },
    ],
  };
  const readReport = async () =>
    JSON.parse(await readFile(directory + "/" + id + ".json", "utf8"));
  async function until(check) {
    const deadline = Date.now() + 5000;
    while (!check()) {
      assert.equal(child.exitCode, null, output);
      assert.ok(Date.now() < deadline, output);
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  }
  async function start(failure = false) {
    output = "";
    replies = [];
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
        env: {
          ...process.env,
          RETRY_TEST: "1",
          COVER_FAILURE_STATUS: failure ? "503" : "",
          RETRY_AFTER: failure && provider === "image" ? "180" : "",
          RETRY_DISCOGS: failure && provider === "discogs" ? "1" : "",
        },
        stdio: ["ignore", "pipe", "pipe", "ipc"],
      },
    );
    child.stdout.on("data", (data) => (output += data));
    child.stderr.on("data", (data) => (output += data));
    await until(() => output.includes("CD recognition listening"));
  }
  async function stop() {
    const exited = once(child, "exit");
    child.kill("SIGTERM");
    await exited;
    child = undefined;
  }
  async function probe() {
    const received = once(socket, "message");
    socket.send(
      Buffer.from(
        JSON.stringify({
          ...toc,
          type: "stroom-cd-toc",
          version: 1,
          generation: 1,
          request: 1,
          track: 1,
          service: "127.0.0.1",
        }),
      ),
      cd.CD_LOOKUP_PORT,
      "127.0.0.1",
    );
    await received;
    assert.equal(replies.at(-1).title, "First track");
  }
  async function advance(milliseconds) {
    const advanced = once(child, "message");
    child.send({ advance: milliseconds });
    await advanced;
  }
  try {
    await saveReport(directory, id, report);
    const failure = ["temporary", "image", "discogs"].includes(provider);
    const delay = provider === "temporary" ? 60000 : 180000;
    await start(failure);
    await probe();
    if (failure) {
      await until(() => output.includes("retry deferred until"));
      const saved = await readReport();
      const retryAt = saved.artworkRetryAt;
      assert.ok(
        retryAt > Date.now() + delay - 10000,
        "Provider's longer retry deadline is saved",
      );
      await stop();
      await start();
      await probe();
      await advance(delay - 30000);
      await probe();
      await new Promise((resolve) => setTimeout(resolve, 100));
      assert.ok(
        !output.includes("FIXTURE_REQUEST"),
        "Restart must retain the longer cooldown",
      );
      assert.equal(
        (await readReport()).artworkRetryAt,
        retryAt,
        "Probes must not reset the deadline",
      );
      await advance(31000);
      await probe();
    }
    await until(() => output.includes("artwork lookup complete"));
    assert.ok(
      replies.some((reply) => reply.artwork_url),
      "Artwork resumes without another minute's delay: " +
        provider +
        "\n" +
        output,
    );
    const saved = await readReport();
    assert.equal(saved.artworkRetryable, false);
    assert.equal(saved.artworkRetryAt, 0);
    await stop();
  } finally {
    if (child) {
      const exited = once(child, "exit");
      child.kill("SIGKILL");
      await exited;
    }
    socket.close();
    await rm(directory, { recursive: true, force: true });
  }
}
console.log(
  "PASS: unfinished/expired artwork resumes, image and Discogs deadlines survive restart, labels remain immediate.",
);
