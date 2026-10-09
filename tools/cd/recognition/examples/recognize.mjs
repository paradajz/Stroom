// Standalone client for the HTTP v2 protocol in ../http-protocol.md.
// Requires Node.js 24 LTS (latest 24.x patch). Protocol: ../http-protocol.md
// Uses a sample TOC; replace it with the complete TOC of your disc.
// Supply the operator's actual base URL; the address below is a placeholder.
// Usage: node tools/cd/recognition/examples/recognize.mjs http://203.0.113.10:12890
const base = new URL(process.argv[2]);
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const MAX_JSON_BYTES = 65536;
const MAX_DELAY_SECONDS = 604800;
const idPattern = /^[A-Za-z0-9._]{27}-$/;

function delay(value, kind = "retry delay") {
  if (value === undefined) return 0;
  if (!Number.isInteger(value) || value < 1 || value > MAX_DELAY_SECONDS)
    throw Error("Invalid " + kind);
  return value;
}
function label(value) {
  return (
    typeof value === "string" &&
    !/[\u0000-\u001F\uD800-\uDFFF]/u.test(value) &&
    Buffer.byteLength(value, "utf8") <= 127
  );
}
function validateArtwork(artwork) {
  if (!artwork || typeof artwork !== "object")
    throw Error("Missing artwork state");
  if (artwork.status === "pending") {
    if (!delay(artwork.retry_after_seconds))
      throw Error("Invalid pending artwork");
  } else if (artwork.status === "available") {
    if (
      typeof artwork.path !== "string" ||
      !/^\/covers\/[A-Za-z0-9._-]+\.jpg$/.test(artwork.path)
    )
      throw Error("Invalid cover path");
  } else if (artwork.status !== "unavailable")
    throw Error("Invalid artwork state");
}
async function api(path, body, timeoutMs = 5000) {
  const url = new URL(path, base);
  if (url.origin !== base.origin) throw Error("Cross-origin API path");
  const response = await fetch(url, {
    method: body === undefined ? "GET" : "POST",
    headers: {
      Accept: "application/json",
      "Accept-Encoding": "identity",
      ...(body === undefined ? {} : { "Content-Type": "application/json" }),
    },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
    signal: AbortSignal.timeout(timeoutMs),
    redirect: "error",
  });
  let retrySeconds = 0;
  try {
    const rawDelay = response.headers.get("retry-after");
    if (rawDelay !== null) {
      const seconds = Number(rawDelay);
      if (
        /^\d+$/.test(rawDelay) &&
        Number.isInteger(seconds) &&
        seconds >= 1 &&
        seconds <= MAX_DELAY_SECONDS
      )
        retrySeconds = seconds;
    }
    if (
      !/^application\/json(?:\s*;\s*charset=utf-8)?$/i.test(
        response.headers.get("content-type") ?? "",
      )
    )
      throw Error("Expected UTF-8 JSON");
    const encoding = response.headers.get("content-encoding");
    if (encoding && encoding !== "identity")
      throw Error("Compressed API response");
    const length = response.headers.get("content-length");
    if (!length || !/^\d+$/.test(length) || Number(length) > MAX_JSON_BYTES)
      throw Error("Invalid response length");
    const chunks = [];
    let used = 0;
    for await (const chunk of response.body) {
      used += chunk.length;
      if (used > MAX_JSON_BYTES || used > Number(length))
        throw Error("Response exceeds API limit");
      chunks.push(chunk);
    }
    if (used !== Number(length)) throw Error("Truncated response");
    const data = JSON.parse(
      new TextDecoder("utf-8", { fatal: true }).decode(Buffer.concat(chunks)),
    );
    if (!data || Array.isArray(data) || data.protocol !== 2)
      throw Error("Unsupported protocol");
    const bodyDelay = delay(data.retry_after_seconds);
    if (response.status === 202 || [429, 503, 502].includes(response.status)) {
      if (!bodyDelay) throw Error("Missing retry delay");
    }
    if (response.status >= 400) {
      if (
        data.status !== "error" ||
        typeof data.error?.code !== "string" ||
        typeof data.error.message !== "string" ||
        typeof data.error.retryable !== "boolean" ||
        data.error.retryable !== [429, 503, 502].includes(response.status)
      )
        throw Error("Invalid error envelope");
    }
    retrySeconds = bodyDelay;
    return { response, data, retrySeconds };
  } catch (error) {
    if (response.body && !response.body.locked)
      await response.body.cancel().catch(() => {});
    // The caller must retain this cooldown if it offers a later retry.
    error.retryAfterSeconds = retrySeconds;
    throw error;
  }
}
function validateLookup(current, expectedId) {
  const { response, data } = current;
  const invalid = (message) => {
    throw Object.assign(Error(message), {
      retryAfterSeconds: current.retrySeconds,
    });
  };
  if (
    data.disc_id !== undefined &&
    (typeof data.disc_id !== "string" ||
      !idPattern.test(data.disc_id) ||
      (expectedId && data.disc_id !== expectedId))
  )
    invalid("Response belongs to another disc");
  if ([200, 202].includes(response.status) && !idPattern.test(data.disc_id))
    invalid("Missing disc identifier");
  if (
    response.status === 202 &&
    (data.status !== "pending" ||
      data.result_path !== "/cd/v2/lookups/" + data.disc_id)
  )
    invalid("Invalid accepted lookup");
}
function failure(current) {
  return Object.assign(Error(current.data.error?.code ?? "Lookup failed"), {
    retryAfterSeconds: current.retrySeconds,
  });
}

const probe = await api("/cd/v2/service", undefined, 3000);
if (
  probe.response.status !== 200 ||
  probe.data.service !== "stroom-cd-recognition" ||
  probe.data.ready !== true
)
  throw failure(probe); // No TOC has been submitted.

const toc = {
  type: "stroom-cd-toc",
  first: 1,
  offsets: [150, 15363],
  leadout: 32314,
};
let current = await api("/cd/v2/lookups", toc); // The only TOC submission.
validateLookup(current);
const accepted = current.response.status === 202;
if (!accepted && current.response.status !== 200) throw failure(current);
const discId = current.data.disc_id;
const waitingUntil = performance.now() + 300000;
while (accepted && [202, 429, 503].includes(current.response.status)) {
  if (!current.retrySeconds) throw Error("Missing retry delay");
  if (performance.now() + current.retrySeconds * 1000 >= waitingUntil)
    throw Object.assign(Error("Lookup waiting period exceeded"), {
      retryAfterSeconds: current.retrySeconds,
    });
  await sleep(current.retrySeconds * 1000);
  const remaining = waitingUntil - performance.now();
  if (remaining <= 0) throw Error("Lookup waiting period exceeded");
  current = await api(
    "/cd/v2/lookups/" + encodeURIComponent(discId),
    undefined,
    Math.min(5000, Math.max(1, Math.floor(remaining))),
  );
  if (performance.now() >= waitingUntil)
    throw Object.assign(Error("Lookup waiting period exceeded"), {
      retryAfterSeconds: current.retrySeconds,
    });
  validateLookup(current, discId);
}
if (current.response.status !== 200) throw failure(current);
if (current.data.status === "no_match") {
  if (!delay(current.data.expires_in_seconds, "no-match expiry"))
    throw Error("Missing no-match expiry");
  console.log("No acceptable match; keep fallback labels");
} else if (current.data.status === "matched") {
  const disc = current.data;
  if (
    !label(disc.album) ||
    !label(disc.artist) ||
    disc.track_count !== toc.offsets.length ||
    !Array.isArray(disc.tracks) ||
    disc.tracks.length !== disc.track_count ||
    !disc.tracks.every(
      (track, i) =>
        track?.number === i + 1 && label(track.title) && label(track.artist),
    )
  )
    throw Error("Invalid track table");
  validateArtwork(disc.artwork);
  // Retain disc locally. Changing tracks requires no further recognition call.
  const track = disc.tracks[0];
  console.log(track.title, track.artist || disc.artist, disc.album);
  console.log("Artwork state:", disc.artwork);
} else throw Error("Unexpected completed lookup state");
