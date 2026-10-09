import { mkdtemp, readFile, writeFile, rm } from "node:fs/promises";
import { join } from "node:path";
import assert from "node:assert/strict";
import { request } from "../../../../../tools/cd/http.mjs";

const original = globalThis.fetch;
const calls = [];
const interval = 250;
globalThis.fetch = async (url) => {
  calls.push({ host: new URL(url).hostname, at: performance.now() });
  return new Response("ok");
};
try {
  await request("https://api.example.test/first", "test", interval);
  const second = request("https://api.example.test/second", "test", interval);
  const third = request("https://api.example.test/third", "test", interval);
  await request("https://images.example.test/cover", "test", interval);
  assert.deepEqual(
    calls.map((call) => call.host),
    ["api.example.test", "images.example.test"],
    "An image host must not wait behind another provider's queue",
  );
  await Promise.all([second, third]);
  const api = calls.filter((call) => call.host === "api.example.test");
  for (let i = 1; i < api.length; i++)
    assert.ok(
      api[i].at - api[i - 1].at >= interval - 15,
      "Same-host calls stay spaced",
    );

  globalThis.fetch = async (url) => {
    calls.push({ host: new URL(url).hostname, at: performance.now() });
    throw Error("network failure");
  };
  await assert.rejects(
    request("https://failure.example.test/one", "test", interval),
    /network failure/,
  );
  await assert.rejects(
    request("https://failure.example.test/two", "test", interval),
    /network failure/,
  );
  assert.ok(
    calls.at(-1).at - calls.at(-2).at >= interval - 15,
    "Failed attempts still count toward provider pacing",
  );
  const realNow = Date.now;
  let now = 1800000000000;
  Date.now = () => now;
  try {
    for (const [index, header] of [
      "180",
      new Date(now + 180000).toUTCString(),
      "invalid",
    ].entries()) {
      now = 1800000000000;
      let attempts = 0;
      globalThis.fetch = async () => {
        attempts++;
        return new Response("busy", {
          status: 429,
          headers: { "Retry-After": header },
        });
      };
      const url = "https://retry-" + index + ".test/";
      await assert.rejects(request(url, "test", 0), (error) => {
        assert.equal(error.retryable, true);
        assert.equal(
          error.retryAt,
          header === "invalid" ? undefined : now + 180000,
        );
        return true;
      });
      if (header !== "invalid") {
        await assert.rejects(request(url, "test", 0), /retry deferred/);
        assert.equal(
          attempts,
          1,
          "Cooldown must prevent further calls to this host",
        );
        now += 181000;
        globalThis.fetch = async () => {
          attempts++;
          return new Response("ok");
        };
        await request(url, "test", 0);
        assert.equal(attempts, 2);
      }
    }
    for (const durations of [
      [300, 60],
      [60, 300],
    ]) {
      const began = now;
      const url = "https://overlap-" + durations.join("-") + ".test/";
      const pending = [];
      let attempts = 0;
      globalThis.fetch = async () => {
        attempts++;
        return new Promise((resolve) => pending.push(resolve));
      };
      // Both requests are in flight before either response establishes a cooldown.
      const first = request(url, "test", 0).catch((error) => error);
      const second = request(url, "test", 0).catch((error) => error);
      assert.equal(attempts, 2);
      pending[0](
        new Response(null, {
          status: 429,
          headers: { "Retry-After": String(durations[0]) },
        }),
      );
      assert.equal((await first).retryAt, began + durations[0] * 1000);
      pending[1](
        new Response(null, {
          status: 429,
          headers: { "Retry-After": String(durations[1]) },
        }),
      );
      assert.equal(
        (await second).retryAt,
        began + 300000,
        "Return the retained deadline to callers as well",
      );
      globalThis.fetch = async () => {
        attempts++;
        return new Response("ok");
      };
      now = began + 61000;
      await assert.rejects(request(url, "test", 0), (error) => {
        assert.equal(error.retryAt, began + 300000);
        return true;
      });
      assert.equal(
        attempts,
        2,
        "Shorter overlapping response must not permit an early request",
      );
      now = began + 301000;
      await request(url, "test", 0);
      assert.equal(
        attempts,
        3,
        "Requests resume after the longest cooldown expires",
      );
    }
  } finally {
    Date.now = realNow;
  }
  console.log(
    "PASS: independent provider pacing, queued same-host calls and failed-attempt spacing.",
  );
} finally {
  globalThis.fetch = original;
}

// Fresh module instances model restarts without retaining any in-memory deadlines.
const directory = await mkdtemp("/tmp/stroom-provider-cooldowns-");
const realNow = Date.now;
let now = 1800000000000;
Date.now = () => now;
try {
  const first = await import("../../../../../tools/cd/http.mjs?before-restart");
  await first.loadProviderCooldowns(directory);
  globalThis.fetch = async () =>
    new Response("busy", {
      status: 429,
      headers: { "Retry-After": "300" },
    });
  await Promise.all(
    ["musicbrainz.org", "coverartarchive.org"].map((host) =>
      assert.rejects(
        first.request("https://" + host + "/disc-a", "test", 0),
        /HTTP 429/,
      ),
    ),
  );
  // Headers establish the cooldown even while the error body is still pending.
  for (const kind of ["broken", "oversized"]) {
    let controller;
    let attempts = 0;
    const url = "https://" + kind + "-body.test/disc-a";
    globalThis.fetch = async () => {
      attempts++;
      return new Response(
        new ReadableStream({
          start(stream) {
            controller = stream;
          },
        }),
        {
          status: 429,
          headers: { "Retry-After": "300" },
        },
      );
    };
    const failed = assert.rejects(first.request(url, "test", 0), (error) => {
      assert.equal(error.retryable, true);
      assert.equal(error.retryAt, now + 300000);
      assert.match(error.message, /HTTP 429/);
      assert.match(
        error.message,
        kind === "broken" ? /connection reset/ : /exceeds 4 MiB/,
      );
      return true;
    });
    await Promise.resolve();
    await assert.rejects(first.request(url, "test", 0), /retry deferred/);
    if (kind === "broken") controller.error(Error("body connection reset"));
    else controller.enqueue(new Uint8Array(4 * 1024 * 1024 + 1));
    await failed;
    await assert.rejects(first.request(url, "test", 0), /retry deferred/);
    assert.equal(
      attempts,
      1,
      "Broken bodies must not permit another provider request",
    );
  }
  const saved = JSON.parse(
    await readFile(join(directory, "provider-cooldowns.json")),
  );
  assert.equal(saved["musicbrainz.org"], now + 300000);
  assert.equal(saved["coverartarchive.org"], now + 300000);
  assert.equal(saved["broken-body.test"], now + 300000);
  assert.equal(saved["oversized-body.test"], now + 300000);

  const restarted =
    await import("../../../../../tools/cd/http.mjs?after-restart");
  await restarted.loadProviderCooldowns(directory);
  const fetched = [];
  globalThis.fetch = async (url) => {
    fetched.push(url);
    return new Response("ok");
  };
  for (const host of Object.keys(saved)) {
    await assert.rejects(
      restarted.request("https://" + host + "/disc-b", "test", 0),
      (error) => error.retryAt === now + 300000 && error.retryable,
    );
  }
  assert.deepEqual(
    fetched,
    [],
    "New disc recognition and artwork both respect restored deadlines",
  );
  await restarted.request("https://unrelated.test/", "test", 0);
  now += 301000;
  for (const host of Object.keys(saved))
    await restarted.request("https://" + host + "/disc-b", "test", 0);
  assert.equal(fetched.length, Object.keys(saved).length + 1);

  const expired = await import("../../../../../tools/cd/http.mjs?expired");
  await expired.loadProviderCooldowns(directory);
  await expired.request("https://musicbrainz.org/disc-c", "test", 0);

  await writeFile(join(directory, "provider-cooldowns.json"), "invalid json");
  const corrupt = await import("../../../../../tools/cd/http.mjs?corrupt");
  await corrupt.loadProviderCooldowns(directory);
  await corrupt.request("https://musicbrainz.org/disc-c", "test", 0);

  // Storage failure does not replace the provider error or disable memory cooldowns.
  const blocked = join(directory, "file-not-directory");
  await writeFile(blocked, "");
  const unavailable =
    await import("../../../../../tools/cd/http.mjs?unavailable");
  await unavailable.loadProviderCooldowns(blocked);
  globalThis.fetch = async () =>
    new Response("busy", {
      status: 503,
      headers: { "Retry-After": "60" },
    });
  await assert.rejects(
    unavailable.request("https://busy.test/", "test", 0),
    /HTTP 503/,
  );
  await assert.rejects(
    unavailable.request("https://busy.test/", "test", 0),
    /retry deferred/,
  );
} finally {
  Date.now = realNow;
  globalThis.fetch = original;
  await rm(directory, { recursive: true, force: true });
}
console.log(
  "PASS: provider cooldown persistence, independent hosts, expiry and storage failures.",
);
