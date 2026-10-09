import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import dgram from "node:dgram";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { cd } from "../../../../../tools/contracts/load.mjs";
import { busyReply } from "../../../../../tools/cd/response.mjs";
import { assertCover } from "./cover_fixture.mjs";

assert.deepEqual(
  busyReply(0x12345678),
  Buffer.from("434442311234567800003a98", "hex"),
);
const directory = await mkdtemp(join(tmpdir(), "stroom-clients-"));
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
    "--out",
    directory,
  ],
  {
    env: {
      ...process.env,
      QUEUE_BLOCK_RECOGNITION: "all",
      QUEUE_TEST_CLOCK: "1",
    },
    stdio: ["ignore", "pipe", "pipe", "ipc"],
  },
);
let output = "";
child.stdout.on("data", (data) => (output += data));
child.stderr.on("data", (data) => (output += data));
function decode(packet) {
  const token = packet.readUInt32BE(4);
  if (packet.subarray(0, 4).toString() === "CDB1") {
    assert.equal(packet.length, 12);
    return { token, busy: true, retryMs: packet.readUInt32BE(8) };
  }
  return {
    token,
    state: packet[cd.CD_LOOKUP_ARTWORK_STATE_OFFSET],
    ...JSON.parse(packet.subarray(cd.CD_LOOKUP_REPLY_HEADER_BYTES)).data,
  };
}
const consoles = Array.from({ length: 4 }, () => {
  const socket = dgram.createSocket("udp4");
  const replies = [];
  socket.on("message", (packet) => replies.push(decode(packet)));
  return { socket, replies };
});
const [a, b, idle, shared] = consoles;
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
function send(console, index, token, track = 1, service = "127.0.0.1") {
  console.socket.send(
    Buffer.from(
      JSON.stringify({
        ...discs[index],
        type: "stroom-cd-toc",
        version: 1,
        generation: index + 1,
        request: token,
        track,
        service,
      }),
    ),
    cd.CD_LOOKUP_PORT,
    "127.0.0.1",
  );
}
const hasReply = (console, token, artwork = false) =>
  console.replies.some(
    (reply) =>
      !reply.busy && reply.token === token && (!artwork || reply.artwork_url),
  );
try {
  // Same IP, different UDP ports: all requests must survive blocked recognition.
  for (const console of consoles) {
    console.socket.bind(0, "127.0.0.1");
    await once(console.socket, "listening");
  }
  await until(() => output.includes("CD recognition listening"));
  send(a, 0, 100);
  await until(() => output.includes("QUEUE_WAIT recognition-0"));
  send(b, 0, 200);
  send(idle, 0, 300);
  send(shared, 0, 400, 2);
  send(a, 0, 101, 2, "127.0.0.2");
  // A cached-label reply below would acknowledge these requests; here the
  // recognition gate deliberately prevents replies until all datagrams arrive.
  await new Promise((resolve) => setTimeout(resolve, 50));
  child.send("recognition-0");
  await until(
    () =>
      hasReply(a, 101) &&
      hasReply(b, 200) &&
      hasReply(idle, 300) &&
      hasReply(shared, 400),
  );
  assert.ok(
    !hasReply(a, 100),
    "Use the latest track and token for each endpoint",
  );
  assert.equal(a.replies[0].title, "Disc 0 track 2");
  assert.equal(b.replies[0].title, "Disc 0 track 1");
  assert.equal(idle.replies[0].title, "Disc 0 track 1");
  assert.ok(
    consoles.every(
      (console) => console.replies[0].state === cd.CD_LOOKUP_ARTWORK_PENDING,
    ),
  );
  await until(() => output.includes("QUEUE_WAIT cover-0"));

  // More than 512 endpoints must receive cached labels and join pending work.
  for (let index = consoles.length; index < 513; ++index) {
    const socket = dgram.createSocket("udp4");
    const replies = [];
    socket.on("message", (packet) => replies.push(decode(packet)));
    const client = { socket, replies };
    consoles.push(client);
    socket.bind(0, "127.0.0.1");
    await once(socket, "listening");
    const received = once(socket, "message", {
      signal: AbortSignal.timeout(5000),
    });
    send(client, 0, 1000 + index);
    await received;
    assert.equal(replies[0].token, 1000 + index);
    assert.equal(replies[0].title, "Disc 0 track 1");
  }
  const overflow = consoles.at(-1);

  // Changing one console's disc must not detach another console from its disc.
  send(a, 1, 102);
  await until(() => output.includes("QUEUE_WAIT recognition-1"));
  send(overflow, 1, 2000, 2);
  await new Promise((resolve) => setTimeout(resolve, 50));
  await until(() =>
    overflow.replies.some((reply) => reply.busy && reply.token === 2000),
  );
  assert.equal(overflow.replies.at(-1).retryMs, 15000);
  child.send("recognition-1");
  await until(() => hasReply(a, 102));
  assert.ok(
    !hasReply(overflow, 2000),
    "No subscription was admitted beyond capacity",
  );
  assert.equal(a.replies.at(-1).album, "Album 1");

  // Expire the third endpoint while refreshing the active subscriptions.
  const advanced = once(child, "message");
  child.send({ advance: 61000 });
  assert.equal((await advanced)[0], "advanced");
  const aBefore = a.replies.length;
  send(b, 0, 201, 2);
  await until(() => hasReply(b, 201));
  assert.equal(
    a.replies.length,
    aBefore,
    "An individual probe replies only to its sender",
  );
  send(shared, 0, 401, 1);
  await until(() => hasReply(shared, 401));
  send(a, 1, 103, 2);
  await until(() => hasReply(a, 103));
  send(overflow, 1, 2001, 2);
  await until(() => hasReply(overflow, 2001));
  // An invalid request must not move B's subscription to a different disc.
  send(b, 1, 999, 99);
  child.send("cover-0");
  await until(() => hasReply(b, 201, true) && hasReply(shared, 401, true));
  assert.equal(shared.replies.at(-1).title, "Disc 0 track 1");
  assert.equal(shared.replies.at(-1).artwork_url, b.replies.at(-1).artwork_url);
  await until(() => output.includes("QUEUE_WAIT cover-1"));
  assert.equal(b.replies.at(-1).title, "Disc 0 track 2");
  assert.ok(
    a.replies
      .filter((reply) => reply.token >= 102)
      .every((reply) => reply.album === "Album 1"),
  );
  assert.ok(
    !a.replies.some((reply) => reply.album === "Album 0" && reply.artwork_url),
    "No obsolete disc update",
  );
  assert.equal(
    idle.replies.length,
    1,
    "Expired subscriptions receive no artwork push",
  );

  child.send("cover-1");
  await until(() => hasReply(a, 103, true) && hasReply(overflow, 2001, true));
  assert.equal(a.replies.at(-1).title, "Disc 1 track 2");
  await assertCover(
    await (await fetch(b.replies.at(-1).artwork_url)).arrayBuffer(),
  );
  await assertCover(
    await (await fetch(a.replies.at(-1).artwork_url)).arrayBuffer(),
  );

  // Rejoin a completed shared job without fetching metadata or covers again.
  send(a, 0, 104, 1, "127.0.0.2");
  send(idle, 0, 301, 2);
  await until(() => hasReply(a, 104, true) && hasReply(idle, 301, true));
  assert.equal(a.replies.at(-1).title, "Disc 0 track 1");
  assert.equal(new URL(a.replies.at(-1).artwork_url).hostname, "127.0.0.2");
  assert.equal(idle.replies.at(-1).title, "Disc 0 track 2");
  for (const index of [0, 1]) {
    assert.equal(
      (output.match(new RegExp("QUEUE_LOOKUP " + index, "g")) ?? []).length,
      1,
    );
    assert.equal(
      (output.match(new RegExp("QUEUE_WAIT cover-" + index, "g")) ?? []).length,
      1,
    );
  }
  // A burst from an admitted endpoint is paced without launching more work.
  let limited = 0;
  for (let index = 0; index < 1500; ++index) {
    const response = once(a.socket, "message", {
      signal: AbortSignal.timeout(5000),
    });
    send(a, 0, 10000 + index);
    const [packet] = await response;
    const result = decode(packet);
    assert.equal(result.token, 10000 + index);
    if (result.busy) {
      ++limited;
      assert.equal(result.retryMs, 15000);
    }
  }
  assert.ok(limited > 0, "Request bursts must receive retry replies");
  assert.equal((output.match(/Received /g) ?? []).length, 2);
  const exited = once(child, "exit");
  child.kill("SIGTERM");
  assert.equal((await exited)[0], 0, output);
} finally {
  child.kill("SIGKILL");
  for (const console of consoles) console.socket.close();
  await rm(directory, { recursive: true, force: true });
}
console.log(
  "PASS: concurrent console labels, track tokens, shared lookups and covers, disc changes, endpoint expiry, rejoining and cached replies at capacity, busy replies and later admission",
);
