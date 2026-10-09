import { readFile, writeFile, rename } from "node:fs/promises";
import { join } from "node:path";
import { setTimeout as delay } from "node:timers/promises";

const REQUEST_INTERVAL_MS = 1100;
const REQUEST_TIMEOUT_MS = 15000;
const RESPONSE_BYTES = 4 * 1024 * 1024;
// API and image hosts have independent request-start schedules.
const lastRequest = new Map();
const retryAfter = new Map();
let cooldownFile;
let cooldownWrites = Promise.resolve();

/** Restore host deadlines before starting recognition or artwork requests. */
export async function loadProviderCooldowns(directory) {
  cooldownFile = directory ? join(directory, "provider-cooldowns.json") : null;
  if (!cooldownFile) return;
  try {
    const saved = JSON.parse(await readFile(cooldownFile, "utf8"));
    if (!saved || typeof saved !== "object" || Array.isArray(saved))
      throw Error("Invalid provider cooldown cache");
    for (const [provider, deadline] of Object.entries(saved)) {
      if (Number.isSafeInteger(deadline) && deadline > Date.now())
        retryAfter.set(
          provider,
          Math.max(retryAfter.get(provider) ?? 0, deadline),
        );
    }
  } catch (error) {
    if (error.code !== "ENOENT")
      console.error("Cannot restore provider cooldowns: " + error.message);
  }
}

function saveProviderCooldowns() {
  if (!cooldownFile) return;
  // Serialize atomic replacements so overlapping responses cannot lose deadlines.
  cooldownWrites = cooldownWrites
    .then(async () => {
      const active = Object.fromEntries(
        [...retryAfter].filter(([, deadline]) => deadline > Date.now()),
      );
      await writeFile(cooldownFile + ".tmp", JSON.stringify(active));
      await rename(cooldownFile + ".tmp", cooldownFile);
    })
    .catch((error) => {
      console.error("Cannot save provider cooldowns: " + error.message);
    });
  return cooldownWrites;
}

function checkRetryAfter(provider) {
  const retryAt = retryAfter.get(provider) ?? 0;
  if (retryAt <= Date.now()) return;
  const error = Error("Provider retry deferred: " + provider);
  error.retryable = true;
  error.retryAt = retryAt;
  throw error;
}

export async function request(
  url,
  userAgent,
  intervalMs = REQUEST_INTERVAL_MS,
  maxBytes = RESPONSE_BYTES,
) {
  const provider = new URL(url).hostname;
  checkRetryAfter(provider);
  const now = performance.now();
  const startAt = Math.max(
    now,
    (lastRequest.get(provider) ?? -Infinity) + intervalMs,
  );
  // Reserve before yielding so overlapping calls to the same host stay spaced.
  lastRequest.set(provider, startAt);
  if (startAt > now) await delay(startAt - now);
  checkRetryAfter(provider);
  const response = await fetch(url, {
    headers: { "User-Agent": userAgent },
    signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
  });
  if (response.status === 404) {
    await response.body?.cancel();
    return null;
  }
  const failure = response.ok ? null : Error(`HTTP ${response.status}: ${url}`);
  if (failure) {
    // A broken or oversized body must not discard the provider's deadline.
    failure.retryable =
      response.status === 408 ||
      response.status === 429 ||
      response.status >= 500;
    if (failure.retryable) {
      const header = response.headers.get("retry-after")?.trim();
      const retryAt =
        header && /^\d+$/.test(header)
          ? Date.now() + Number(header) * 1000
          : Date.parse(header ?? "");
      if (Number.isSafeInteger(retryAt) && retryAt > Date.now()) {
        failure.retryAt = Math.max(retryAfter.get(provider) ?? 0, retryAt);
        retryAfter.set(provider, failure.retryAt);
        await saveProviderCooldowns();
      }
    }
  }
  const chunks = [];
  let bytes = 0;
  try {
    for await (const chunk of response.body ?? []) {
      bytes += chunk.length;
      if (bytes > maxBytes) {
        const error = Error(
          `Service response exceeds ${maxBytes / (1024 * 1024)} MiB`,
        );
        error.retryable = false;
        throw error;
      }
      chunks.push(chunk);
    }
  } catch (error) {
    if (!failure) throw error;
    failure.message += " — " + error.message;
    throw failure;
  }
  const body = Buffer.concat(chunks);
  if (failure) {
    let detail = body.toString();
    try {
      detail = JSON.parse(detail).error ?? detail;
    } catch {
      // Non-JSON errors still get a bounded, single-line explanation.
    }
    detail = String(detail)
      .replace(/[\x00-\x1f\x7f]/gu, " ")
      .slice(0, 512);
    if (detail) failure.message += " — " + detail;
    throw failure;
  }
  return {
    bytes: body,
    type: response.headers.get("content-type") ?? "",
  };
}
