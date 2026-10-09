import { cd } from "../../contracts/load.mjs";
import { SERVICE_LIMITS } from "./config.mjs";

export const RESULT_CACHE_BYTES = SERVICE_LIMITS.memory.max_size;
export const RETAINED_REPORT_BYTES = 2 * 1024 * 1024;
export const REPORT_BYTES = 16 * 1024 * 1024;
export const MAX_CANDIDATES = 64;
export const MAX_MEDIA = 32;

export function reportLimit(message) {
  return Object.assign(Error(message), { code: "CD_REPORT_LIMIT" });
}

/** Reject excessive input instead of silently selecting from truncated evidence. */
export function checkCandidates(candidates = []) {
  if (!Array.isArray(candidates) || candidates.length > MAX_CANDIDATES)
    throw reportLimit("CD report exceeds " + MAX_CANDIDATES + " candidates");
  for (const candidate of candidates) {
    if (!Array.isArray(candidate.media) || candidate.media.length > MAX_MEDIA)
      throw reportLimit("CD candidate exceeds " + MAX_MEDIA + " media");
    for (const medium of candidate.media)
      if (
        !Array.isArray(medium.tracks) ||
        medium.tracks.length > cd.CD_MAX_TRACKS
      )
        throw reportLimit("CD medium exceeds " + cd.CD_MAX_TRACKS + " tracks");
  }
}

export function reportText(report, maxBytes = REPORT_BYTES, pretty = false) {
  const text = JSON.stringify(report, null, pretty ? 2 : undefined);
  if (Buffer.byteLength(text) > maxBytes)
    throw reportLimit("CD report exceeds " + maxBytes + " bytes");
  return text;
}

export function checkReport(report) {
  checkCandidates(report.candidates);
  checkCandidates(report.coverFallback?.candidates);
  reportText(report);
}

const pick = (value, keys) =>
  Object.fromEntries(
    keys
      .filter((key) => value?.[key] !== undefined)
      .map((key) => [key, value[key]]),
  );
const candidateKeys = [
  "id",
  "source",
  "title",
  "artist",
  "artists",
  "date",
  "country",
  "url",
  "media",
  "comparison",
  "verification",
  "coverUrl",
  "coverUrls",
  "coverFile",
  "coverAvailable",
  "coverRetryable",
  "coverRetryAt",
  "coverError",
  "coverSource",
  "coverRejectedUrls",
];

/** Keep delivery, matching and retry state; raw provider diagnostics stay on disk. */
export function compactReport(report) {
  checkCandidates(report.candidates);
  checkCandidates(report.coverFallback?.candidates);
  const compact = pick(report, [
    "discId",
    "toc",
    "lookedUpAt",
    "match",
    "selected",
    "candidateCount",
    "lookupMilliseconds",
    "fuzzyLimits",
    "note",
    "artworkRetryable",
    "artworkRetryAt",
    "lookupStatus",
    "lookupRetryAt",
    "error",
    "failedAt",
    "errors",
  ]);
  if (report.candidates)
    compact.candidates = report.candidates.map((candidate) =>
      pick(candidate, candidateKeys),
    );
  for (const name of ["gnudb", "discogs"])
    if (report[name]) compact[name] = pick(report[name], ["errors", "skipped"]);
  if (report.coverFallback) {
    compact.coverFallback = pick(report.coverFallback, [
      "errors",
      "rejections",
      "error",
      "retryable",
      "retryAt",
    ]);
    if (report.coverFallback.candidates)
      compact.coverFallback.candidates = report.coverFallback.candidates.map(
        (candidate) => pick(candidate, candidateKeys),
      );
  }
  // Detach mutable working data as well as accounting its serialized UTF-8 size.
  const text = reportText(compact, RETAINED_REPORT_BYTES);
  return { report: JSON.parse(text), bytes: Buffer.byteLength(text) };
}

/** LRU accounting for retained result data, independent of disk-cache eviction. */
export class ResultCache {
  #entries = new Map();
  #bytes = 0;
  #evictions = 0;

  constructor({ maxBytes = RESULT_CACHE_BYTES, onEvict = () => {} } = {}) {
    Object.assign(this, { maxBytes, onEvict });
  }

  oldestIdle(exclude) {
    for (const { job } of this.#entries.values())
      if (job.id !== exclude?.id && !job.working && !job.pending) return job;
  }

  store(job, compact) {
    const previous = this.#entries.get(job.id);
    if (compact.bytes > this.maxBytes) return false;
    while (
      this.#bytes - (previous?.bytes ?? 0) + compact.bytes >
      this.maxBytes
    ) {
      const victim = this.oldestIdle(job);
      if (!victim) return false;
      this.evict(victim.id);
    }
    this.delete(job.id);
    job.report = compact.report;
    this.#entries.set(job.id, { job, bytes: compact.bytes });
    this.#bytes += compact.bytes;
    return true;
  }

  touch(id) {
    const entry = this.#entries.get(id);
    if (!entry) return;
    this.#entries.delete(id);
    this.#entries.set(id, entry);
  }

  delete(id) {
    const entry = this.#entries.get(id);
    if (!entry) return;
    this.#bytes -= entry.bytes;
    this.#entries.delete(id);
  }

  evict(id) {
    if (!this.#entries.has(id)) return false;
    this.delete(id);
    ++this.#evictions;
    this.onEvict(id);
    return true;
  }

  snapshot() {
    return {
      bytes: this.#bytes,
      maxBytes: this.maxBytes,
      reports: this.#entries.size,
      evictions: this.#evictions,
    };
  }
}
