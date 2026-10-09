import assert from "node:assert/strict";
import { mkdtemp, writeFile, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
  JobDeadline,
  checkSignal,
  combineSignals,
  waitWithSignal,
  RECOGNITION_TIMEOUT_MS,
  ARTWORK_TIMEOUT_MS,
} from "../../../../../tools/cd/recognition/deadline.mjs";
import { request } from "../../../../../tools/cd/recognition/http.mjs";
import { selectDisc } from "../../../../../tools/cd/recognition/lookup.mjs";
import { verifyDiscogs } from "../../../../../tools/cd/recognition/discogs.mjs";
import { prepareCover } from "../../../../../tools/cd/recognition/image.mjs";
import { monitor } from "../../../../../tools/cd/recognition/monitor.mjs";

assert.equal(RECOGNITION_TIMEOUT_MS, 60000);
assert.equal(ARTWORK_TIMEOUT_MS, 120000);
let now = 10;
const job = new JobDeadline("recognition", RECOGNITION_TIMEOUT_MS, {
  now: () => now,
});
now += 59999;
checkSignal(job.signal, { retryAt: 123456 });
now++;
assert.throws(() => checkSignal(job.signal), {
  code: "CD_JOB_TIMEOUT",
  retryAt: 123456,
});
job.dispose();
const disposed = new JobDeadline("recognition", 5);
disposed.dispose();
await new Promise((resolve) => setTimeout(resolve, 15));
assert.equal(
  disposed.signal.aborted,
  false,
  "Completed jobs must clear their timer",
);
const a = new AbortController(),
  b = new AbortController();
const reason = Object.assign(Error("expired"), {
  code: "CD_JOB_TIMEOUT",
  retryable: true,
});
const combined = combineSignals([a.signal, b.signal]);
b.abort(reason);
assert.equal(combined.signal.reason, reason);
combined.dispose();
const shared = new AbortController();
const pending = waitWithSignal(new Promise(() => {}), shared.signal);
shared.abort(reason);
await assert.rejects(pending, (error) => error === reason);

const originalFetch = globalThis.fetch;
const originalFinished = monitor.providerFinished;
const completed = [];
monitor.providerFinished = (host, detail) =>
  completed.push({ host, ...detail });
try {
  let calls = 0;
  globalThis.fetch = async () => {
    ++calls;
    return new Response("ok");
  };
  await request("https://paced.deadline.test/", "test", 60000);
  const paced = new AbortController();
  const waiting = request(
    "https://paced.deadline.test/",
    "test",
    60000,
    undefined,
    paced.signal,
  );
  paced.abort(reason);
  await assert.rejects(waiting, (error) => error === reason);
  assert.equal(calls, 1, "Cancellation during pacing must prevent the fetch");

  let bodyStarted;
  const started = new Promise((resolve) => (bodyStarted = resolve));
  globalThis.fetch = async (_, { signal }) =>
    new Response(
      new ReadableStream({
        start(controller) {
          signal.addEventListener(
            "abort",
            () => controller.error(signal.reason),
            { once: true },
          );
          bodyStarted();
        },
      }),
    );
  const body = new AbortController();
  const reading = request(
    "https://body.deadline.test/",
    "test",
    0,
    undefined,
    body.signal,
  );
  await started;
  body.abort(reason);
  await assert.rejects(reading, (error) => error === reason);
  assert.equal(completed.at(-1).cancelled, true);
  assert.equal(completed.at(-1).failed, false);
  assert.equal(completed.at(-1).timeout, false);

  const timed = new JobDeadline("recognition", 10);
  const hold = setTimeout(() => {}, 1000);
  try {
    await assert.rejects(
      request(
        "https://timer.deadline.test/",
        "test",
        0,
        undefined,
        timed.signal,
      ),
      { code: "CD_JOB_TIMEOUT" },
    );
  } finally {
    timed.dispose();
    clearTimeout(hold);
  }

  const toc = { first: 1, offsets: [150], leadout: 15000 };
  const hosts = [];
  const lookup = new AbortController();
  globalThis.fetch = async (url, { signal }) => {
    hosts.push(new URL(url).hostname);
    lookup.abort(reason);
    signal.throwIfAborted();
  };
  await assert.rejects(
    selectDisc(toc, {
      userAgent: "test",
      contact: "test@example.test",
      signal: lookup.signal,
    }),
    (error) => error === reason,
  );
  assert.deepEqual(
    hosts,
    ["musicbrainz.org"],
    "An expired lookup cannot start other providers",
  );

  const discogs = new AbortController();
  globalThis.fetch = async (_, { signal }) => {
    discogs.abort(reason);
    signal.throwIfAborted();
  };
  await assert.rejects(
    verifyDiscogs([{ title: "Album", artist: "Artist", tracks: ["Track"] }], {
      userAgent: "test",
      signal: discogs.signal,
    }),
    (error) => error === reason,
  );

  // An earlier provider's cooldown survives cancellation in the next provider.
  const cooldown = new AbortController();
  const cooldownReason = Object.assign(Error("expired after rate limit"), {
    code: "CD_JOB_TIMEOUT",
  });
  const until = Date.now() + 3600000;
  globalThis.fetch = async (url) => {
    if (new URL(url).hostname === "musicbrainz.org")
      return new Response(null, {
        status: 429,
        headers: { "Retry-After": "3600" },
      });
    cooldown.abort(cooldownReason);
    throw cooldownReason;
  };
  await assert.rejects(
    selectDisc(toc, {
      userAgent: "test",
      contact: "test@example.test",
      signal: cooldown.signal,
    }),
    (error) => error === cooldownReason && error.retryAt >= until,
  );
} finally {
  globalThis.fetch = originalFetch;
  monitor.providerFinished = originalFinished;
}

// Use a real child that ignores SIGTERM to verify cancellation kills and reaps it.
const directory = await mkdtemp(join(tmpdir(), "stroom-conversion-deadline-"));
const originalPath = process.env.PATH;
let pid;
try {
  const pidFile = join(directory, "pid");
  await writeFile(
    join(directory, "ffmpeg"),
    `#!${process.execPath}\nconst {writeFileSync} = require('node:fs');\nprocess.on('SIGTERM',()=>{});\nwriteFileSync(${JSON.stringify(pidFile)},String(process.pid));\nsetInterval(()=>{},1000);\n`,
    { mode: 0o755 },
  );
  process.env.PATH = directory + ":" + originalPath;
  const conversion = new AbortController();
  const preparing = prepareCover(Buffer.from("fixture"), {
    signal: conversion.signal,
  });
  const rejected = assert.rejects(preparing, (error) => error === reason);
  const end = Date.now() + 5000;
  while (!pid) {
    try {
      pid = Number(await readFile(pidFile, "utf8"));
    } catch {}
    assert.ok(Date.now() < end, "Converter did not start");
    if (!pid) await new Promise((resolve) => setTimeout(resolve, 5));
  }
  conversion.abort(reason);
  await rejected;
  assert.throws(
    () => process.kill(pid, 0),
    { code: "ESRCH" },
    "The converter must exit before the promise rejects",
  );
} finally {
  process.env.PATH = originalPath;
  if (pid) {
    try {
      process.kill(pid, "SIGKILL");
    } catch {}
  }
  await rm(directory, { recursive: true, force: true });
}
console.log("CD job deadline and cancellation checks passed");
