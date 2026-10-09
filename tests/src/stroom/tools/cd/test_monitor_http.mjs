import assert from "node:assert/strict";
import { request } from "../../../../../tools/cd/recognition/http.mjs";
import { monitor } from "../../../../../tools/cd/recognition/monitor.mjs";
import { lookupDisc } from "../../../../../tools/cd/recognition/musicbrainz.mjs";
import { lookupCtdb } from "../../../../../tools/cd/recognition/ctdb.mjs";
import { lookupGnudb } from "../../../../../tools/cd/recognition/gnudb.mjs";
import { verifyDiscogs } from "../../../../../tools/cd/recognition/discogs.mjs";

const originalFetch = globalThis.fetch;
const originalTimeout = AbortSignal.timeout;
const originalNow = performance.now.bind(performance);
let pacing = 0;
Object.defineProperty(performance, "now", {
  configurable: true,
  value: () => originalNow() + pacing,
});
function respond(response) {
  globalThis.fetch = async () => {
    pacing += 5000;
    if (response instanceof Error) throw response;
    return response;
  };
}
try {
  respond(new Response("ok"));
  await request("https://good.test/", "test", 0);
  respond(new Response("missing", { status: 404 }));
  assert.equal(await request("https://good.test/missing", "test", 0), null);
  assert.equal(monitor.providers()["good.test"].requests, 2);
  assert.equal(monitor.providers()["good.test"].failures, 0);
  assert.equal(monitor.providers()["good.test"].notFound, 1);

  respond(Error("network failure"));
  await assert.rejects(
    request("https://bad.test/", "test", 0),
    /network failure/,
  );
  respond(new Response("unavailable", { status: 503 }));
  await assert.rejects(request("https://bad.test/", "test", 0), /HTTP 503/);
  respond(
    new Response("limited", { status: 429, headers: { "Retry-After": "300" } }),
  );
  await assert.rejects(request("https://bad.test/", "test", 0), /HTTP 429/);
  await assert.rejects(
    request("https://bad.test/", "test", 0),
    /retry deferred/,
  );
  assert.equal(monitor.providers()["bad.test"].requests, 3);
  assert.equal(monitor.providers()["bad.test"].failures, 3);
  assert.equal(monitor.providers()["bad.test"].rateLimited, 1);
  assert.equal(monitor.providers()["bad.test"].deferred, 1);
  assert.ok(monitor.providers()["bad.test"].cooldownUntil);
  respond(new Response("oversized"));
  await assert.rejects(request("https://size.test/", "test", 0, 2), /exceeds/);
  assert.equal(monitor.providers()["size.test"].failures, 1);

  AbortSignal.timeout = () =>
    AbortSignal.abort(new DOMException("Timed out", "TimeoutError"));
  globalThis.fetch = async (url, { signal }) => {
    throw signal.reason;
  };
  await assert.rejects(
    request("https://timeout.test/", "test", 0),
    /Timed out/,
  );
  assert.equal(monitor.providers()["timeout.test"].timeouts, 1);
  assert.equal(monitor.providers()["timeout.test"].inFlight, 0);
  AbortSignal.timeout = originalTimeout;

  const toc = { first: 1, offsets: [150, 15363], leadout: 32314 };
  const options = { userAgent: "test", contact: "test@example.test" };
  respond(Response.json({ releases: [] }));
  assert.equal((await lookupDisc(toc, options)).selected, null);
  respond(Response.json({ broken: true }));
  await assert.rejects(
    lookupDisc(toc, options),
    /Unexpected MusicBrainz response/,
  );
  respond(new Response("broken JSON"));
  await assert.rejects(lookupDisc(toc, options), SyntaxError);
  respond(new Response("bad gateway", { status: 502 }));
  await assert.rejects(lookupDisc(toc, options), /HTTP 502/);
  assert.equal(monitor.providers()["musicbrainz.org"].requests, 4);
  assert.equal(monitor.providers()["musicbrainz.org"].failures, 1);
  assert.equal(
    monitor.providers()["musicbrainz.org"].protocolErrors,
    2,
    "Do not double-count transport failures caught by a provider parser",
  );

  respond(new Response("202 No match\n"));
  assert.deepEqual((await lookupGnudb(toc, options)).candidates, []);
  respond(new Response("not a protocol response"));
  await assert.rejects(lookupGnudb(toc, options), /GnuDB query failed/);
  assert.equal(monitor.providers()["gnudb.gnudb.org"].protocolErrors, 1);
  respond(new Response("<ctdb></ctdb>"));
  assert.deepEqual((await lookupCtdb(toc, options)).candidates, []);
  respond(new Response("invalid XML"));
  await assert.rejects(lookupCtdb(toc, options), /Invalid CTDB XML/);
  assert.equal(monitor.providers()["db.cue.tools"].protocolErrors, 1);
  respond(new Response("invalid JSON"));
  await assert.rejects(
    verifyDiscogs(
      [{ artist: "Artist", title: "Album", tracks: ["One", "Two"] }],
      options,
    ),
    /JSON/,
  );
  assert.equal(monitor.providers()["api.discogs.com"].protocolErrors, 1);
  const requests = monitor.providers()["api.discogs.com"].requests;
  const seed = { artist: "Artist", title: "Album", tracks: ["One", "Two"] };
  await assert.rejects(
    verifyDiscogs([seed], options, [
      { search: seed, response: { results: "invalid cached result" } },
    ]),
    /Invalid Discogs search response/,
  );
  assert.equal(monitor.providers()["api.discogs.com"].requests, requests);
  assert.equal(
    monitor.providers()["api.discogs.com"].protocolErrors,
    1,
    "Invalid retained metadata is not a new provider response failure",
  );
  console.log(
    "PASS: HTTP failures/timeouts/429s, cooldown skips, expected absence, oversized bodies and provider parsing errors are counted without duplication.",
  );
} finally {
  globalThis.fetch = originalFetch;
  AbortSignal.timeout = originalTimeout;
  Object.defineProperty(performance, "now", { value: originalNow });
}
