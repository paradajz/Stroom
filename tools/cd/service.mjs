#!/usr/bin/env node
import { cd, artwork } from "../contracts/load.mjs";
import { createServer } from "node:http";
import { metadataReply, busyReply } from "./response.mjs";
import dgram from "node:dgram";
import { parseArgs } from "node:util";
import { mkdir, readFile, stat } from "node:fs/promises";
import { resolve, join } from "node:path";
import { isIPv4 } from "node:net";
import { parseToc, discId } from "./disc.mjs";
import { resolveCover } from "./cover.mjs";
import { cacheDirectory, cachedReport, saveReport } from "./cache.mjs";
import { loadProviderCooldowns } from "./http.mjs";
import { selectDisc } from "./lookup.mjs";

const RETRY_MS = 60000;
const MAX_DISCS = 128;
const MAX_CLIENTS = 512;
const MAX_CACHE_READS = 8;
const MAX_UDP_SENDS = 256;
const BUSY_RETRY_MS = 15000;
const CLIENT_IDLE_MS = 60000;
const usage = `Usage: node tools/cd/service.mjs --contact EMAIL_OR_URL [--ps2 ADDRESS] [--out DIRECTORY] [--refresh]
CD recognition UDP listener on port ${cd.CD_LOOKUP_PORT}. Prints candidates and saves JSON; returns selected track metadata and serves covers to the PS2.`;
let options;
try {
  options = parseArgs({
    options: {
      contact: { type: "string" },
      ps2: { type: "string" },
      out: { type: "string", default: cacheDirectory() },
      refresh: { type: "boolean", default: false },
      help: { type: "boolean", short: "h" },
    },
  }).values;
  if (options.help) {
    console.log(usage);
    process.exit(0);
  }
  if (
    !options.contact?.trim() ||
    /[\x00-\x1f\x7f]/u.test(options.contact) ||
    (options.ps2 !== undefined && !isIPv4(options.ps2))
  )
    throw Error(usage);
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
let directory = resolve(options.out);
try {
  await mkdir(directory, { recursive: true });
} catch (error) {
  console.error(
    `Cache unavailable at ${directory}: ${error.message}; continuing with labels only. Disk cache and artwork are disabled until restart.`,
  );
  directory = null;
}
await loadProviderCooldowns(directory);
const userAgent = `stroom-cd/0.1 (${options.contact})`;
const socket = dgram.createSocket("udp4");
const discs = new Map();
// Disc work is shared; each UDP endpoint retains its own current request.
const clients = new Map();
function expireClients() {
  const now = Date.now();
  for (const [key, client] of clients)
    if (now - client.lastSeen >= CLIENT_IDLE_MS) clients.delete(key);
}
const clientCleanup = setInterval(expireClients, CLIENT_IDLE_MS / 2);
clientCleanup.unref();
let cacheReads = 0,
  coverReads = 0,
  udpSends = 0;
const sourceRates = new Map();
const globalRate = { tokens: 1024, at: Date.now() };
function consume(bucket, rate) {
  const now = Date.now();
  bucket.tokens = Math.min(
    1024,
    bucket.tokens + (Math.max(0, now - bucket.at) * rate) / 1000,
  );
  bucket.at = now;
  if (bucket.tokens < 1) return false;
  --bucket.tokens;
  return true;
}
function admitRequest(address) {
  if (!consume(globalRate, 1000)) return false;
  let bucket = sourceRates.get(address);
  if (!bucket) {
    if (sourceRates.size >= 1024)
      sourceRates.delete(sourceRates.keys().next().value);
    bucket = { tokens: 1024, at: Date.now() };
    sourceRates.set(address, bucket);
  }
  return consume(bucket, 100);
}
function sendPacket(packet, client) {
  if (!packet || closing || udpSends >= MAX_UDP_SENDS) return;
  ++udpSends;
  socket.send(packet, client.port, client.peer, (error) => {
    --udpSends;
    if (error) console.error("CD reply failed: " + error.message);
  });
}
function busy(client) {
  sendPacket(busyReply(client.request.request, BUSY_RETRY_MS), client);
}
const http = createServer(async (req, res) => {
  const name = req.url?.startsWith("/covers/")
    ? req.url.slice("/covers/".length)
    : "";
  if (
    req.method !== "GET" ||
    !directory ||
    !/^[A-Za-z0-9._-]+\.jpg$/u.test(name)
  ) {
    res.writeHead(404).end();
    return;
  }
  if (coverReads >= MAX_CACHE_READS) {
    res.writeHead(503, { "Retry-After": "15" }).end();
    return;
  }
  ++coverReads;
  try {
    const info = await stat(join(directory, name));
    if (
      !info.isFile() ||
      info.size < 4 ||
      info.size > artwork.ARTWORK_MAX_BYTES
    ) {
      res.writeHead(404).end();
      return;
    }
    const bytes = await readFile(join(directory, name));
    res.writeHead(200, {
      "Content-Type": "image/jpeg",
      "Content-Length": bytes.length,
    });
    res.end(bytes);
  } catch {
    res.writeHead(404).end();
  } finally {
    --coverReads;
  }
});
http.requestTimeout = 5000;
http.headersTimeout = 5000;
http.on("error", (error) =>
  console.error(`Cover HTTP unavailable: ${error.message}`),
);
http.listen(cd.CD_LOOKUP_PORT, "0.0.0.0");
socket.on("close", () => {
  clearInterval(clientCleanup);
  http.close();
  http.closeAllConnections();
});
function reply(job, client) {
  if (closing || !job.report) return;
  const packet = metadataReply(
    job.report,
    client.request,
    cd.CD_LOOKUP_PORT,
    http.listening,
  );
  sendPacket(packet, client);
}
let recognitionBusy = false;
let artworkBusy = false;
let closing = false;

function publish(job, report) {
  job.report = report;
  job.recognitionComplete = true;
  expireClients();
  for (const client of clients.values())
    if (client.discId === job.id) reply(job, client);
}

// Disk reads do not wait for the provider queue. Register the job first so
// repeated probes share the job instead of starting duplicate lookups.
async function loadCachedOrQueue(job) {
  const useCache = directory && !options.refresh;
  if (useCache && cacheReads >= MAX_CACHE_READS) {
    job.cachePending = job.pending = true;
    return;
  }
  job.cachePending = job.pending = false;
  job.working = true;
  let cached = null;
  if (useCache) {
    ++cacheReads;
    try {
      cached = await cachedReport(directory, job.id);
    } finally {
      --cacheReads;
    }
  }
  job.working = false;
  if (closing) {
    processQueues();
    return;
  }
  if (cached) {
    publish(job, cached);
    console.log(job.id + ": cache hit");
    if (cached.artworkRetryable && cached.artworkRetryAt <= Date.now())
      job.pending = true;
  } else job.pending = true;
  processQueues();
}

async function persistReport(job, report) {
  if (!directory) return false;
  try {
    await saveReport(directory, job.id, report);
    return true;
  } catch (error) {
    console.error(job.id + ": could not save report: " + error.message);
    return false;
  }
}

function logReport(job, report, saved) {
  console.log(
    `${job.id}: ${report.match.toUpperCase()}, ${report.candidateCount} candidate(s), ${report.lookupMilliseconds} ms`,
  );
  if (report.selected) {
    const winner = report.candidates[0];
    console.log(
      `  BEST MATCH: ${winner.artist} — ${winner.title} (disc ${report.selected.mediumPosition})`,
    );
  }
  for (const candidate of report.candidates) {
    console.log(
      `  ${candidate.artist} — ${candidate.title} [${candidate.date ?? "?"}, ${candidate.country ?? "?"}] ${candidate.url}`,
    );
    const score = candidate.comparison;
    if (score.rejection) console.log(`    REJECTED: ${score.rejection}`);
    const seconds = (value) =>
      value === null ? "unknown" : (value / 1000).toFixed(3) + "s";
    console.log(
      `    Closest disc ${score.mediumPosition ?? "?"}: track-count difference ${score.trackCountDifference}, missing timings ${score.missingDurations}, average difference ${seconds(score.meanDifferenceMs)}, largest ${seconds(score.maxDifferenceMs)}`,
    );
    for (const medium of candidate.media) {
      console.log(
        `    Disc ${medium.position}${medium.exactDisc ? " (exact disc ID)" : ""}`,
      );
      for (const track of medium.tracks)
        console.log(`      ${track.number}. ${track.title}`);
    }
    if (candidate.coverFile)
      console.log(`    Cover: ${join(directory, candidate.coverFile)}`);
    if (candidate.coverError)
      console.log(`    Cover unavailable: ${candidate.coverError}`);
  }
  console.log(
    `  ${report.note}\n  Report: ${saved ? join(directory, `${job.id}.json`) : "not saved"}`,
  );
}

// Evict idle jobs to make room; running and queued jobs stay within MAX_DISCS.
function trimIdleDiscs(incoming = 0) {
  while (discs.size + incoming > MAX_DISCS) {
    const evict = [...discs.entries()].find(
      ([, job]) => !job.working && !job.pending,
    );
    if (!evict) return;
    discs.delete(evict[0]);
    for (const [key, client] of clients)
      if (client.discId === evict[0]) clients.delete(key);
  }
}

function nextJob(artwork) {
  const eligible = (job) =>
    job?.pending &&
    !job.cachePending &&
    !job.working &&
    job.recognitionComplete === artwork;
  return [...discs.values()].find(eligible);
}

// One recognition and one artwork job may run concurrently. HTTP pacing remains
// shared per provider; a slow image host never owns the recognition queue.
function processQueues() {
  if (closing) {
    if (!recognitionBusy && !artworkBusy && !cacheReads) socket.close();
    return;
  }
  for (const job of discs.values()) {
    if (cacheReads >= MAX_CACHE_READS) break;
    if (job.cachePending && !job.working) void loadCachedOrQueue(job);
  }
  void processRecognition();
  void processArtwork();
}

async function processRecognition() {
  if (recognitionBusy || closing) return;
  const job = nextJob(false);
  if (!job) return;
  recognitionBusy = true;
  job.pending = false;
  job.working = true;
  const began = performance.now();
  try {
    const report = await selectDisc(job.toc, {
      userAgent,
      contact: options.contact,
    });
    report.lookupMilliseconds = Math.round(performance.now() - began);
    report.artworkRetryable = Boolean(report.selected && directory);
    publish(job, report);
    // Finish this save before handing the job to artwork, so its later report
    // cannot race with an initial metadata-only write to the same cache file.
    const saved = await persistReport(job, report);
    if (report.artworkRetryable) {
      job.pending = true;
      job.initialArtwork = true;
    } else {
      logReport(job, report, saved);
    }
  } catch (error) {
    job.retryAt = Date.now() + RETRY_MS;
    console.error(
      `${job.id}: lookup failed: ${error.message}; retry on a later probe in 60s`,
    );
    await persistReport(job, {
      discId: job.id,
      toc: job.toc,
      error: error.message,
      failedAt: new Date().toISOString(),
    });
  } finally {
    job.working = false;
    recognitionBusy = false;
    trimIdleDiscs();
    processQueues();
  }
}

async function processArtwork() {
  if (artworkBusy || closing) return;
  const job = nextJob(true);
  if (!job) return;
  artworkBusy = true;
  job.pending = false;
  job.working = true;
  try {
    const report = structuredClone(job.report);
    await resolveCover(report, { userAgent, directory });
    if (report.artworkRetryable)
      report.artworkRetryAt = Math.max(
        Date.now() + RETRY_MS,
        report.artworkRetryAt,
      );
    publish(job, report);
    const saved = await persistReport(job, report);
    if (job.initialArtwork) {
      logReport(job, report, saved);
      job.initialArtwork = false;
    } else {
      console.log(
        job.id +
          ": artwork " +
          (report.artworkRetryable
            ? `retry deferred until ${new Date(report.artworkRetryAt).toISOString()}`
            : "lookup complete"),
      );
    }
  } catch (error) {
    job.report.artworkRetryable = true;
    job.report.artworkRetryAt = Math.max(
      Date.now() + RETRY_MS,
      error.retryAt ?? 0,
    );
    await persistReport(job, job.report);
    console.error(job.id + ": artwork retry failed: " + error.message);
  } finally {
    job.working = false;
    artworkBusy = false;
    trimIdleDiscs();
    processQueues();
  }
}

async function cachedOrBusy(client) {
  if (!directory || options.refresh || cacheReads >= MAX_CACHE_READS) {
    busy(client);
    return;
  }
  ++cacheReads;
  try {
    const report = await cachedReport(directory, client.discId);
    // An asynchronous read must not reply for an endpoint that changed discs.
    const key = client.peer + ":" + client.port;
    const current = clients.get(key);
    if (current && current.request.request !== client.request.request) return;
    if (report) {
      reply({ report }, client);
      if (report.artworkRetryable) busy(client);
    } else busy(client);
  } finally {
    --cacheReads;
    processQueues();
  }
}

socket.on("message", (data, peer) => {
  if (closing || (options.ps2 !== undefined && peer.address !== options.ps2))
    return;
  let toc;
  try {
    toc = parseToc(data);
  } catch (error) {
    console.error(`Ignored invalid TOC: ${error.message}`);
    return;
  }
  const request = JSON.parse(data.toString());
  const id = discId(toc);
  if (
    !Number.isInteger(request.request) ||
    request.request < 1 ||
    request.request > 0xffffffff ||
    !Number.isInteger(request.track) ||
    request.track < 1 ||
    request.track > toc.offsets.length ||
    typeof request.service !== "string" ||
    !isIPv4(request.service)
  )
    return;
  expireClients();
  const clientKey = peer.address + ":" + peer.port;
  const client = {
    discId: id,
    request,
    peer: peer.address,
    port: peer.port,
    lastSeen: Date.now(),
  };
  if (!admitRequest(peer.address)) {
    busy(client);
    return;
  }
  const canSubscribe = clients.has(clientKey) || clients.size < MAX_CLIENTS;
  const existing = discs.get(id);
  if (existing) {
    if (existing.report) reply(existing, client);
    if (!canSubscribe) {
      if (!existing.report || existing.report.artworkRetryable) busy(client);
      return;
    }
    clients.set(clientKey, client);
    const retryAt = existing.recognitionComplete
      ? existing.report.artworkRetryable
        ? (existing.report.artworkRetryAt ?? 0)
        : null
      : existing.retryAt || null;
    if (
      !existing.working &&
      !existing.pending &&
      retryAt !== null &&
      Date.now() >= retryAt
    ) {
      existing.retryAt = 0;
      if (existing.recognitionComplete) {
        existing.pending = true;
        processQueues();
      } else {
        void loadCachedOrQueue(existing);
      }
    }
    return;
  }
  if (!canSubscribe) {
    void cachedOrBusy(client);
    return;
  }
  trimIdleDiscs(1);
  if (discs.size >= MAX_DISCS) {
    clients.delete(clientKey);
    void cachedOrBusy(client);
    return;
  }
  const job = {
    id,
    toc,
    pending: false,
    recognitionComplete: false,
    retryAt: 0,
  };
  clients.set(clientKey, client);
  discs.set(id, job);
  console.log(
    `Received ${toc.offsets.length}-track TOC from ${peer.address}, disc ID ${id}`,
  );
  void loadCachedOrQueue(job);
});
socket.on("error", (error) => {
  console.error(error.message);
  process.exitCode = 1;
  closing = true;
  processQueues();
});
for (const signal of ["SIGINT", "SIGTERM"])
  process.on(signal, () => {
    if (closing) return;
    closing = true;
    processQueues();
  });
socket.bind(cd.CD_LOOKUP_PORT, "0.0.0.0", () =>
  console.log(
    `CD recognition listening on UDP ${cd.CD_LOOKUP_PORT}, accepting ${options.ps2 ?? "any console address"}; reports: ${directory ?? "disabled (labels only)"}`,
  ),
);
