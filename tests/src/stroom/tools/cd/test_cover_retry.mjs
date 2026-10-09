import { cd } from "../../../../../tools/contracts/load.mjs";
import { assertCover } from "./cover_fixture.mjs";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import dgram from "node:dgram";
import { mkdtemp, rm, readdir, readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

for (const status of [503, 404]) {
  const directory = await mkdtemp("/tmp/stroom-cover-retry-");
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
      "--ps2",
      "127.0.0.1",
      "--out",
      directory,
    ],
    {
      env: { ...process.env, COVER_FAILURE_STATUS: String(status) },
      stdio: ["ignore", "pipe", "pipe", "ipc"],
    },
  );
  const socket = dgram.createSocket("udp4");
  let output = "";
  const replies = [];
  socket.on("message", (packet) =>
    replies.push({
      ...JSON.parse(packet.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES)).data,
      state: packet[cd.CD_LOOKUP_ARTWORK_STATE_OFFSET],
    }),
  );
  child.stdout.on("data", (data) => (output += data));
  child.stderr.on("data", (data) => (output += data));
  async function until(check) {
    const deadline = Date.now() + 10000;
    while (!check()) {
      assert.equal(child.exitCode, null, output);
      assert.ok(Date.now() < deadline, output);
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  }
  const probe = Buffer.from(
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
  async function send() {
    const received = once(socket, "message");
    socket.send(probe, cd.CD_LOOKUP_PORT, "127.0.0.1");
    await received;
  }
  try {
    await until(() => output.includes("CD recognition listening"));
    await send();
    await until(() => output.includes("Report:"));
    assert.equal(replies[0].state, cd.CD_LOOKUP_ARTWORK_PENDING);
    assert.equal(
      replies.at(-1).state,
      status === 503
        ? cd.CD_LOOKUP_ARTWORK_PENDING
        : cd.CD_LOOKUP_ARTWORK_UNAVAILABLE,
    );
    assert.equal(replies.at(-1).title, "First track");
    assert.equal(replies.at(-1).artwork_url, "");
    const requests = () => (output.match(/FIXTURE_REQUEST/g) ?? []).length;
    const before = requests();
    await send();
    assert.equal(requests(), before, "No immediate retry");
    const advanced = once(child, "message");
    child.send("advance");
    await advanced;
    await send();
    if (status === 503) {
      await until(() => replies.some((reply) => reply.artwork_url));
      assert.equal(
        requests(),
        before + 1,
        "Retry only the cover, not recognition",
      );
      assert.equal(replies.at(-1).state, cd.CD_LOOKUP_ARTWORK_AVAILABLE);
      await assertCover(
        await (await fetch(replies.at(-1).artwork_url)).arrayBuffer(),
      );
      await send();
      assert.equal(requests(), before + 1, "Successful artwork stays cached");
    } else {
      await new Promise((resolve) => setTimeout(resolve, 200));
      assert.equal(
        requests(),
        before,
        "Confirmed missing cover must not retry",
      );
    }
    const files = await readdir(directory);
    const report = JSON.parse(
      await readFile(
        directory + "/" + files.find((file) => file.endsWith(".json")),
        "utf8",
      ),
    );
    assert.equal(report.artworkRetryable, false);
    const exited = once(child, "exit");
    child.kill("SIGTERM");
    await exited;
  } finally {
    child.kill("SIGKILL");
    socket.close();
    await rm(directory, { recursive: true, force: true });
  }
}
console.log(
  "PASS: delayed artwork-only recovery, preserved labels, successful caching and no retry for missing covers.",
);
