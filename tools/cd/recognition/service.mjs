#!/usr/bin/env node
import { cd } from "../../contracts/load.mjs";
import { createServer } from "node:http";
import {
  lookupResponse,
  artworkState,
  pendingResponse,
  apiError,
  sendJson,
  readTocBody,
  ID_PATTERN,
} from "./api.mjs";
import { parseArgs } from "node:util";
import { mkdir, readFile, stat } from "node:fs/promises";
import { join } from "node:path";
import { discId } from "./disc.mjs";
import { resolveCover, coverRetryAt } from "./cover.mjs";
import { cachedReport, saveReport, unsuccessfulDeadline } from "./cache.mjs";
import { DiskCacheQuota, cacheFileDisc } from "./quota.mjs";
import { loadProviderCooldowns } from "./http.mjs";
import { selectDisc } from "./lookup.mjs";
import { LookupQueue } from "./lookup-queue.mjs";
import { monitor, queueSnapshot, MONITOR_INTERVAL_MS } from "./monitor.mjs";
import { ResultCache, checkReport, compactReport } from "./report.mjs";
import { HttpLimits, HTTP_LIMITS } from "./http-limits.mjs";
import { loadConfig, SERVICE_LIMITS, cacheTarget } from "./config.mjs";
import { CoverBandwidth } from "./cover-bandwidth.mjs";
import { JobDeadline, checkSignal } from "./deadline.mjs";

const MAX_CACHE_READS = 8;
const BUSY_RETRY_MS = 15000;
const CACHE_PRUNE_MS = 30000;
const usage = `Usage: node tools/cd/recognition/service.mjs [--config FILE] [--refresh]
CD recognition HTTP v2 service on TCP ${cd.CD_LOOKUP_PORT}. Returns whole-disc metadata and serves covers.
Reads ~/.config/stroom/config.json (or $XDG_CONFIG_HOME/stroom/config.json).
On first run, creates the file; set contact before starting. Warnings and errors are always printed.`;
let options;
let config;
let configurationPath;
try {
  options = parseArgs({
    options: {
      config: { type: "string" },
      refresh: { type: "boolean", default: false },
      help: { type: "boolean", short: "h" },
    },
  }).values;
  if (options.help) {
    console.log(usage);
    process.exit(0);
  }
  const path = options.config ?? (process.env.STROOM_CD_CONFIG || undefined);
  ({ config, path: configurationPath } = loadConfig(path, {
    initialize: !path,
  }));
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
const MAX_DISCS = SERVICE_LIMITS.memory.max_results;
const COVER_DOWNLOAD_TIMEOUT_MS = SERVICE_LIMITS.covers.timeout;
const RETRY_MS = SERVICE_LIMITS.lookup.artwork_retry;
const coverBandwidth = new CoverBandwidth(
  config.cover_speed,
  SERVICE_LIMITS.covers.burst,
);
monitor.log = (event) => {
  if (event.event === "summary" && !config.log_statistics) return;
  const line = "CD monitor: " + JSON.stringify(event);
  if (event.event === "warning" || event.event === "critical")
    console.error(line);
  else console.log(line);
};
let directory = config.cache_directory;
try {
  await mkdir(directory, { recursive: true });
} catch (error) {
  monitor.storageFailure("cache-init", error);
  console.error(
    `Cache unavailable at ${directory}: ${error.message}; continuing with labels only. Disk cache and artwork are disabled until restart.`,
  );
  directory = null;
}
await loadProviderCooldowns(directory);
const userAgent = `stroom-cd/0.1 (${config.contact})`;
const discs = new Map();
const lookups = new LookupQueue();
const results = new ResultCache({
  onEvict: (id) => forgetDisc(id),
});
function forgetDisc(id) {
  results.delete(id);
  discs.delete(id);
}
const quota = new DiskCacheQuota(directory, {
  maxBytes: config.cache_max_size,
  targetBytes: cacheTarget(config.cache_max_size),
  isProtected: (id) => discs.get(id)?.working || discs.get(id)?.pending,
  onEvict: (id) => {
    forgetDisc(id);
    console.log(id + ": disk cache evicted");
  },
  onError: (error, operation) => {
    monitor.storageFailure("quota-" + operation, error);
    console.error("Cache quota: " + error.message);
  },
  onSuccess: (operation) => monitor.storageSuccess("quota-" + operation),
});
await quota.initialize();
const quotaCleanup = setInterval(() => void quota.prune(), CACHE_PRUNE_MS);
quotaCleanup.unref();
let cacheReads = 0;
const httpLimits = new HttpLimits();
const handleHttp = async (req, res) => {
  const caller = req.socket.remoteAddress;
  req.socket.setTimeout(COVER_DOWNLOAD_TIMEOUT_MS);
  res.shouldKeepAlive = false;
  res.setHeader("Connection", "close");
  const fail = (status, code, message, seconds, id, extra) =>
    sendJson(res, apiError(status, code, message, seconds, id), extra);
  const denial = httpLimits.request(
    req.socket,
    req.method === "GET" && req.url?.startsWith("/covers/"),
  );
  if (denial) {
    if (denial.status)
      fail(
        denial.status,
        denial.status === 429 ? "rate_limited" : "busy",
        "HTTP request allowance exhausted",
        denial.retryAfter,
      );
    return;
  }
  if (closing) {
    fail(503, "service_unavailable", "Service unavailable", 15);
    return;
  }
  try {
    const path = req.url;
    const isSubmit = path === "/cd/v2/lookups";
    const isProbe = path === "/cd/v2/service";
    const resultMatch = /^\/cd\/v2\/lookups\/([^/]+)$/.exec(path);
    const name = path?.startsWith("/covers/") ? path.slice(8) : "";
    const isCover = /^[A-Za-z0-9._-]+\.jpg$/u.test(name);
    const known = isSubmit || isProbe || resultMatch || isCover;
    if (
      req.headers["transfer-encoding"] ||
      req.headers.expect ||
      ((!isSubmit || req.method !== "POST") &&
        req.headers["content-length"] !== undefined &&
        req.headers["content-length"] !== "0")
    ) {
      ++httpLimits.rejected.unsupported;
      fail(400, "invalid_request", "Unsupported request framing");
      return;
    }
    if (!known) {
      fail(404, "not_found", "Unknown route");
      return;
    }
    const method = isSubmit ? "POST" : "GET";
    if (req.method !== method) {
      fail(
        405,
        "method_not_allowed",
        "Unsupported method",
        undefined,
        undefined,
        { Allow: method },
      );
      return;
    }
    if (isProbe) {
      sendJson(res, {
        status: 200,
        body: { protocol: 2, service: "stroom-cd-recognition", ready: true },
      });
      return;
    }
    if (isSubmit || resultMatch) {
      let toc, id;
      if (isSubmit) {
        toc = await readTocBody(req, httpLimits.deadline(req.socket));
        id = discId(toc);
      } else {
        id = resultMatch[1];
        if (!ID_PATTERN.test(id)) {
          fail(400, "invalid_request", "Invalid disc identifier");
          return;
        }
      }
      const job = await findLookup(id, toc, caller);
      if (!job) {
        fail(
          404,
          "result_unavailable",
          "Result is not retained",
          undefined,
          id,
        );
        return;
      }
      // A retained match can recover artwork without another recognition job.
      if (
        directory &&
        job.report?.selected &&
        discs.get(id) === job &&
        !job.working &&
        !job.pending &&
        job.report.artworkRetryable &&
        (job.report.artworkRetryAt ?? 0) <= Date.now()
      ) {
        queueArtwork(job);
        processQueues();
      }
      sendJson(
        res,
        job.report
          ? lookupResponse(job.report, Boolean(directory))
          : pendingResponse(id),
      );
      return;
    }
    if (!directory) {
      fail(404, "not_found", "Cover unavailable");
      return;
    }
    await sendCover(req, res, name);
  } catch (error) {
    if (!res.destroyed && !res.headersSent) {
      if (error.status)
        fail(error.status, error.code, error.message, error.retryAfter);
      else {
        console.error("CD HTTP request failed: " + error.message);
        fail(503, "busy", "Request could not be completed", 15);
      }
    } else if (!res.destroyed) res.destroy();
  }
};
async function sendCover(req, res, name) {
  const releaseDownload = httpLimits.cover(req.socket);
  if (!releaseDownload) {
    sendJson(
      res,
      apiError(503, "busy", "Cover transfer capacity exhausted", 15),
    );
    return;
  }
  const controller = new AbortController();
  let reading = true;
  let responseDone = false;
  let released = false;
  const releaseSlot = () => {
    // A disconnected reader may still have an asynchronous file operation in
    // progress. Keep it counted until both the response and that work settle.
    if (!released && responseDone && !reading) {
      released = true;
      releaseDownload();
    }
  };
  const deadline = setTimeout(() => {
    controller.abort();
    res.destroy();
  }, COVER_DOWNLOAD_TIMEOUT_MS);
  deadline.unref();
  const responseEnded = () => {
    responseDone = true;
    clearTimeout(deadline);
    controller.abort();
    releaseSlot();
  };
  res.once("finish", responseEnded);
  res.once("close", responseEnded);
  const id = cacheFileDisc(name);
  const release = quota.hold(id);
  try {
    await quota.idle();
    if (res.destroyed) return;
    const info = await stat(join(directory, name));
    if (res.destroyed) return;
    if (
      !info.isFile() ||
      info.size < 4 ||
      info.size > cd.CD_LOOKUP_COVER_BYTES
    ) {
      sendJson(res, apiError(404, "not_found", "Cover unavailable"));
      return;
    }
    const bytes = await readFile(join(directory, name), {
      signal: controller.signal,
    });
    if (res.destroyed) return;
    monitor.storageSuccess("cover-read");
    await quota.touch(id);
    if (res.destroyed) return;
    res.writeHead(200, {
      "Content-Type": "image/jpeg",
      "Content-Length": bytes.length,
    });
    await coverBandwidth.send(res, bytes, controller.signal);
  } catch (error) {
    if (
      !controller.signal.aborted &&
      !res.headersSent &&
      error.code &&
      error.code !== "ENOENT" &&
      error.code !== "ABORT_ERR"
    )
      monitor.storageFailure("cover-read", error);
    if (!res.destroyed && !res.headersSent)
      sendJson(res, apiError(404, "not_found", "Cover unavailable"));
    else if (!res.destroyed) res.destroy();
  } finally {
    reading = false;
    release();
    releaseSlot();
  }
}
const http = createServer(
  { maxHeaderSize: 2048, rejectNonStandardBodyWrites: true },
  handleHttp,
);
http.maxConnections = HTTP_LIMITS.connections;
http.maxRequestsPerSocket = 1;
http.prependListener("connection", (connection) => {
  if (httpLimits.connection(connection))
    connection.setTimeout(HTTP_LIMITS.headersMilliseconds, () =>
      connection.destroy(),
    );
});
http.on("drop", () => ++httpLimits.rejected.connectionsGlobal);
http.on("dropRequest", (_req, connection) => {
  ++httpLimits.rejected.pipelined;
  connection.destroy();
});
http.on("clientError", (_error, connection) => {
  if (!connection.destroyed) {
    httpLimits.request(connection);
    ++httpLimits.rejected.malformed;
    connection.destroy();
  }
});
for (const event of ["connect", "upgrade"])
  http.on(event, (req, connection) => {
    httpLimits.request(connection);
    ++httpLimits.rejected.unsupported;
    connection.destroy();
  });
http.on("checkContinue", handleHttp);
http.on("checkExpectation", handleHttp);
http.requestTimeout = HTTP_LIMITS.headersMilliseconds;
http.headersTimeout = HTTP_LIMITS.headersMilliseconds;
http.on("error", (error) => {
  console.error(`CD HTTP unavailable: ${error.message}`);
  process.exitCode = 1;
  closing = true;
  processQueues();
});
let recognitionBusy = false;
let artworkBusy = false;
let closing = false;

const sampleMonitor = () =>
  monitor.sample({
    queues: queueSnapshot(discs.values()),
    cache: quota.snapshot(),
    memoryCache: results.snapshot(),
    http: httpLimits.snapshot(),
    directory,
  });
const monitorTimer = setInterval(
  () => void sampleMonitor(),
  MONITOR_INTERVAL_MS,
);
monitorTimer.unref();

function queueArtwork(job) {
  job.queuedAt = performance.now();
  job.pending = true;
}

function publish(job, report, signal) {
  checkSignal(signal);
  checkReport(report);
  const compact = compactReport(report);
  checkSignal(signal);
  const retained = results.store(job, compact);
  job.discardAfterWork = !retained;
  job.recognitionComplete = true;
  return retained;
}

function busyError() {
  return Object.assign(Error("Lookup capacity exhausted"), {
    status: 503,
    code: "busy",
    retryAfter: BUSY_RETRY_MS / 1000,
  });
}

// The shared loading promise resolves only after cache lookup and provider
// admission. A 202 must never acknowledge work which admission later rejects.
async function findLookup(id, toc, caller) {
  let existing = discs.get(id);
  if (existing?.loading) await existing.loading;
  existing = discs.get(id);
  if (existing) {
    const expiry = unsuccessfulDeadline(existing.report);
    if (expiry && expiry <= Date.now()) {
      if (!toc) return null;
      if (existing.working || existing.pending) throw busyError();
      forgetDisc(id);
    } else {
      results.touch(id);
      void quota.touch(id);
      return existing;
    }
  }
  if (cacheReads >= MAX_CACHE_READS) throw busyError();
  trimIdleDiscs(1);
  const retained = discs.size < MAX_DISCS;
  const job = {
    id,
    toc,
    caller,
    pending: false,
    working: true,
    recognitionComplete: false,
  };
  if (retained) discs.set(id, job);
  ++cacheReads;
  const release = quota.hold(id);
  job.loading = (async () => {
    try {
      await quota.idle();
      const cached =
        directory && (!toc || !options.refresh)
          ? await cachedReport(directory, id)
          : null;
      if (cached) {
        void quota.touch(id);
        job.toc = cached.toc;
        job.fullReportSaved = true;
        if (retained) {
          if (!publish(job, cached)) {
            forgetDisc(id);
            job.report = compactReport(cached).report;
          }
        } else {
          job.report = compactReport(cached).report;
          job.recognitionComplete = true;
        }
        console.log(id + ": cache hit");
      } else if (toc && !closing && retained && lookups.enqueue(job, caller)) {
        job.pending = true;
        job.queuedAt = performance.now();
        console.log(id + ": lookup queued for " + caller);
      } else {
        if (retained) forgetDisc(id);
        if (toc) throw busyError();
        return null;
      }
      return job;
    } catch (error) {
      if (retained && !job.report && !job.pending) forgetDisc(id);
      throw error;
    } finally {
      --cacheReads;
      release();
      job.working = false;
      delete job.loading;
      processQueues();
    }
  })();
  return await job.loading;
}

async function persistReport(job, report) {
  job.fullReportSaved = false;
  if (!directory) return false;
  try {
    await saveReport(directory, job.id, report);
    job.fullReportSaved = true;
    monitor.storageSuccess("report-write");
    return true;
  } catch (error) {
    monitor.storageFailure("report-write", error);
    console.error(job.id + ": could not save report: " + error.message);
    return false;
  } finally {
    await quota.record(job.id + ".json", job.id + ".json.tmp");
  }
}

// Provider metadata must not turn a compact result into multiple terminal lines.
function logText(value, limit) {
  let result = "";
  let count = 0;
  for (const char of String(value ?? "")) {
    if (count++ === limit) return result + "…";
    result += /[\x00-\x1f\x7f-\x9f\u2028\u2029]/u.test(char) ? " " : char;
  }
  return result;
}

function logReport(job, report, saved) {
  const selected = report.candidates.find(
    (candidate) => candidate.id === report.selected?.releaseId,
  );
  const outcome = selected
    ? `${report.match.toUpperCase()}: ${logText(selected.artist, 100)} — ${logText(selected.title, 160)}; disc ${report.selected.mediumPosition}, ${report.toc.offsets.length} tracks; artwork ${artworkState(report, Boolean(directory)).status}`
    : `no match; ${report.candidateCount} candidate(s)`;
  console.log(
    `${job.id}: ${outcome}; ${report.lookupMilliseconds} ms; Report: ${saved ? logText(join(directory, `${job.id}.json`), 512) : "not saved"}`,
  );
}

// Evict idle jobs to make room; running and queued jobs stay within MAX_DISCS.
function trimIdleDiscs(incoming = 0) {
  while (discs.size + incoming > MAX_DISCS) {
    const oldest = results.oldestIdle();
    const evict = oldest
      ? [oldest.id, oldest]
      : [...discs.entries()].find(([, job]) => !job.working && !job.pending);
    if (!evict) return;
    if (!results.evict(evict[0])) forgetDisc(evict[0]);
  }
}

function nextArtwork() {
  const eligible = (job) =>
    job?.pending && !job.working && job.recognitionComplete;
  return [...discs.values()].find(eligible);
}

// One recognition and one artwork job may run concurrently. HTTP pacing remains
// shared per provider; a slow image host never owns the recognition queue.
function processQueues() {
  if (closing) {
    if (!recognitionBusy && !artworkBusy && !cacheReads) {
      clearInterval(quotaCleanup);
      clearInterval(monitorTimer);
      http.close();
      http.closeAllConnections();
    }
    return;
  }
  void processRecognition();
  void processArtwork();
}

async function processRecognition() {
  if (recognitionBusy || closing) return;
  const job = lookups.next();
  if (!job) return;
  recognitionBusy = true;
  job.running = "recognition";
  job.pending = false;
  job.working = true;
  const began = performance.now();
  const deadline = new JobDeadline(
    "recognition",
    SERVICE_LIMITS.lookup.recognition_timeout,
  );
  try {
    const report = await selectDisc(job.toc, {
      userAgent,
      contact: config.contact,
      signal: deadline.signal,
    });
    report.lookupMilliseconds = Math.round(performance.now() - began);
    report.artworkRetryable = Boolean(report.selected && directory);
    if (!report.selected) {
      report.lookupStatus = "no-match";
      report.lookupRetryAt = Date.now() + SERVICE_LIMITS.cache.no_match_ttl;
    }
    const retained = publish(job, report, deadline.signal);
    deadline.dispose();
    monitor.lookup(report.selected ? "matched" : "noMatch");
    // Finish this save before handing the job to artwork, so its later report
    // cannot race with an initial metadata-only write to the same cache file.
    const saved = await persistReport(job, report);
    if (retained && report.artworkRetryable) {
      queueArtwork(job);
      job.initialArtwork = true;
    } else {
      logReport(job, report, saved);
    }
  } catch (error) {
    deadline.dispose();
    monitor.lookup("failed");
    const report = {
      discId: job.id,
      toc: job.toc,
      error: error.message,
      failedAt: new Date().toISOString(),
      lookupStatus: "error",
      lookupRetryAt: Math.max(
        Date.now() + SERVICE_LIMITS.cache.failure_retry,
        Number.isSafeInteger(error.retryAt) ? error.retryAt : 0,
      ),
    };
    publish(job, report);
    console.error(
      `${job.id}: lookup failed: ${error.message}; retry on a later probe after ${new Date(report.lookupRetryAt).toISOString()}`,
    );
    await persistReport(job, report);
  } finally {
    deadline.dispose();
    lookups.finish(job);
    delete job.running;
    job.working = false;
    if (job.discardAfterWork) forgetDisc(job.id);
    recognitionBusy = false;
    void quota.prune();
    trimIdleDiscs();
    processQueues();
  }
}

async function processArtwork() {
  if (artworkBusy || closing) return;
  const job = nextArtwork();
  if (!job) return;
  artworkBusy = true;
  job.running = "artwork";
  job.pending = false;
  job.working = true;
  const deadline = new JobDeadline(
    "artwork",
    SERVICE_LIMITS.lookup.artwork_timeout,
  );
  let report;
  try {
    // Full diagnostics are read only by the active artwork worker. A failed save
    // must not resurrect an older on-disk version over newer in-memory state.
    report = job.fullReportSaved
      ? await cachedReport(directory, job.id, { signal: deadline.signal })
      : null;
    report ??= structuredClone(job.report);
    await resolveCover(report, {
      userAgent,
      directory,
      onCoverWrite: (name) => quota.record(name),
      signal: deadline.signal,
    });
    if (report.artworkRetryable)
      report.artworkRetryAt = Math.max(
        Date.now() + RETRY_MS,
        report.artworkRetryAt,
      );
    publish(job, report, deadline.signal);
    deadline.dispose();
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
    deadline.dispose();
    try {
      checkReport(report);
      compactReport(report);
    } catch {
      report = structuredClone(job.report);
    }
    report.artworkRetryable = true;
    report.artworkRetryAt = Math.max(
      Date.now() + RETRY_MS,
      error.retryAt ?? 0,
      coverRetryAt(report),
    );
    try {
      publish(job, report);
    } catch {
      // Retain already published labels while finishing this attempt, then let
      // a later request reload the saved report instead of exceeding RAM bounds.
      job.discardAfterWork = true;
    }
    await persistReport(job, report);
    console.error(job.id + ": artwork retry failed: " + error.message);
  } finally {
    deadline.dispose();
    delete job.running;
    job.working = false;
    if (job.discardAfterWork) forgetDisc(job.id);
    artworkBusy = false;
    void quota.prune();
    trimIdleDiscs();
    processQueues();
  }
}

for (const signal of ["SIGINT", "SIGTERM"])
  process.on(signal, () => {
    if (closing) return;
    closing = true;
    processQueues();
  });
http.listen(cd.CD_LOOKUP_PORT, "0.0.0.0", () => {
  console.log(
    `CD recognition listening on HTTP TCP ${cd.CD_LOOKUP_PORT}, accepting any client address; reports: ${directory ?? "disabled (labels only)"}; config: ${configurationPath}`,
  );
  void sampleMonitor();
});
