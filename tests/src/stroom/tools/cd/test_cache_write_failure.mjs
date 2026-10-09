import { cd } from "../../../../../tools/contracts/load.mjs";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import dgram from "node:dgram";
import { fileURLToPath } from "node:url";
import { discId } from "../../../../../tools/cd/disc.mjs";
import { assertCover } from "./cover_fixture.mjs";

const toc = { first: 1, offsets: [150, 15363], leadout: 32314 };
for (const stage of ["startup", "initial", "completed", "retry"]) {
  const directory = await mkdtemp("/tmp/stroom-cache-write-");
  const outputDirectory =
    stage === "startup" ? `${directory}/blocked/cache` : directory;
  if (stage === "startup")
    await writeFile(`${directory}/blocked`, "not a directory");
  const blockSave = () => mkdir(`${directory}/${discId(toc)}.json.tmp`);
  if (stage === "initial") await blockSave();
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
      outputDirectory,
    ],
    {
      env: {
        ...process.env,
        COVER_FAILURE_STATUS:
          stage === "retry" || stage === "startup" ? "503" : "",
      },
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
      ...JSON.parse(packet.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES)).data,
    }),
  );
  async function waitFor(condition) {
    const deadline = Date.now() + 5000;
    while (!condition()) {
      assert.equal(child.exitCode, null, output);
      assert.ok(Date.now() < deadline, output);
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  }
  const send = (token, track = 1) =>
    socket.send(
      Buffer.from(
        JSON.stringify({
          ...toc,
          type: "stroom-cd-toc",
          version: 1,
          generation: 1,
          request: token,
          track,
          service: "127.0.0.1",
        }),
      ),
      cd.CD_LOOKUP_PORT,
      "127.0.0.1",
    );
  try {
    await waitFor(() => output.includes("CD recognition listening"));
    send(1);
    await waitFor(() => replies.length > 0);
    assert.equal(replies[0].title, "First track");
    if (stage === "startup") {
      await waitFor(() => output.includes("Report: not saved"));
      assert.match(output, /Cache unavailable.*continuing with labels only/);
      assert.ok(!output.includes("FIXTURE_COVER_WAIT"), output);
      assert.ok(replies.every((reply) => reply.artwork_url === ""));
      const calls = (output.match(/FIXTURE_REQUEST/g) ?? []).length;
      assert.equal(calls, 1, "Only metadata should be requested");
      const advanced = once(child, "message");
      child.send("advance");
      await advanced;
      send(2, 2);
      await waitFor(() => replies.some((reply) => reply.token === 2));
      assert.equal(
        replies.find((reply) => reply.token === 2).title,
        "Second track",
      );
      assert.ok(replies.every((reply) => reply.artwork_url === ""));
      assert.equal((output.match(/FIXTURE_REQUEST/g) ?? []).length, calls);
      assert.equal((output.match(/Cache unavailable/g) ?? []).length, 1);
      assert.ok(!output.includes("could not save report:"), output);
      const exited = once(child, "exit");
      child.kill("SIGTERM");
      const [code] = await exited;
      assert.equal(code, 0, output);
      continue;
    }
    if (stage === "retry") {
      await waitFor(() => output.includes("Report:"));
      await blockSave();
      const advanced = once(child, "message");
      child.send("advance");
      await advanced;
      send(2);
    } else {
      await waitFor(() => output.includes("FIXTURE_COVER_WAIT"));
      if (stage === "completed") await blockSave();
      child.send("release-cover");
    }
    await waitFor(() => replies.some((reply) => reply.artwork_url));
    await waitFor(() =>
      output.includes(
        stage === "retry" ? "artwork lookup complete" : "Report: not saved",
      ),
    );
    const metadata = replies.find((reply) => reply.artwork_url);
    await assertCover(await (await fetch(metadata.artwork_url)).arrayBuffer());
    assert.ok(output.includes("could not save report:"), output);
    assert.ok(!output.includes("lookup failed:"), output);
    assert.ok(!output.includes("artwork retry failed:"), output);
    const calls = (output.match(/FIXTURE_REQUEST/g) ?? []).length;
    send(3);
    await waitFor(() =>
      replies.some((reply) => reply.token === 3 && reply.artwork_url),
    );
    assert.equal((output.match(/FIXTURE_REQUEST/g) ?? []).length, calls);
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
  "PASS: startup cache failure preserves labels; report-write failures preserve early labels, completed covers, artwork retries and in-memory reuse.",
);
