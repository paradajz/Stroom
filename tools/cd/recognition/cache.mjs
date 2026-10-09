import { join } from "node:path";
export { cacheDirectory } from "./config.mjs";
import { SERVICE_LIMITS } from "./config.mjs";
import { readFile, writeFile, rename, stat, open } from "node:fs/promises";
import { discId } from "./disc.mjs";
import { cd } from "../../contracts/load.mjs";
import { coverDimensions } from "./image.mjs";
import { monitor } from "./monitor.mjs";
import { checkSignal } from "./deadline.mjs";
import {
  REPORT_BYTES,
  checkReport,
  compactReport,
  reportText,
} from "./report.mjs";
const CACHE_VERSION = 2;
export const NO_MATCH_TTL_MS = SERVICE_LIMITS.cache.no_match_ttl;
export const LOOKUP_FAILURE_RETRY_MS = SERVICE_LIMITS.cache.failure_retry;

/** Unsuccessful outcomes have explicit, fixed deadlines independent of LRU touches. */
export function unsuccessfulDeadline(report) {
  if (
    report?.selected ||
    !Number.isSafeInteger(report?.lookupRetryAt) ||
    report.lookupRetryAt <= 0
  )
    return 0;
  if (
    report.lookupStatus === "error" &&
    typeof report.error === "string" &&
    report.error
  )
    return report.lookupRetryAt;
  if (
    report.lookupStatus === "no-match" &&
    report.selected === null &&
    Array.isArray(report.candidates) &&
    !report.error &&
    !Object.keys(report.errors ?? {}).length &&
    !report.gnudb?.errors?.length &&
    !report.discogs?.errors?.length
  )
    return report.lookupRetryAt;
  return 0;
}

export async function saveReport(directory, id, report) {
  const path = join(directory, id + ".json");
  checkReport(report);
  const text =
    reportText(
      { ...report, cacheVersion: CACHE_VERSION },
      REPORT_BYTES - 1,
      true,
    ) + "\n";
  await writeFile(path + ".tmp", text);
  await rename(path + ".tmp", path);
}

async function readReport(path, signal) {
  checkSignal(signal);
  const file = await open(path, "r");
  try {
    const info = await file.stat();
    checkSignal(signal);
    if (!info.isFile() || info.size > REPORT_BYTES) return null;
    // Read at most the checked size plus one byte, even if another process grows
    // the file. Never allocate an unbounded buffer or parse a partial report.
    const buffer = Buffer.alloc(info.size + 1);
    let used = 0;
    while (used < buffer.length) {
      const { bytesRead } = await file.read(
        buffer,
        used,
        buffer.length - used,
        null,
      );
      checkSignal(signal);
      if (!bytesRead) break;
      used += bytesRead;
    }
    return used > info.size ? null : buffer.subarray(0, used).toString("utf8");
  } finally {
    await file.close();
  }
}

/** Reuse recognized labels or unexpired unsuccessful outcomes; artwork is independent. */
export async function cachedReport(directory, id, { signal } = {}) {
  try {
    const text = await readReport(join(directory, id + ".json"), signal);
    checkSignal(signal);
    if (text === null) return null;
    monitor.storageSuccess("cache-read");
    const report = JSON.parse(text);
    checkReport(report);
    compactReport(report);
    checkSignal(signal);
    if (
      report.cacheVersion !== CACHE_VERSION ||
      report.discId !== id ||
      discId(report.toc) !== id
    )
      return null;
    const deadline = unsuccessfulDeadline(report);
    if (deadline) return deadline > Date.now() ? report : null;
    if (!report.selected || !Array.isArray(report.candidates)) return null;
    const winner = report.candidates.find(
      (item) => item.id === report.selected.releaseId,
    );
    const medium = winner?.media?.find(
      (item) => item.position === report.selected.mediumPosition,
    );
    if (
      !medium ||
      medium.tracks?.length !== report.toc.offsets.length ||
      typeof winner.title !== "string" ||
      typeof winner.artist !== "string" ||
      !medium.tracks.every(
        (track) =>
          typeof track.title === "string" &&
          (track.artist === undefined || typeof track.artist === "string"),
      )
    )
      return null;
    for (const candidate of report.candidates) {
      if (candidate !== winner) delete candidate.coverFile;
    }
    if (winner.coverFile) {
      let valid =
        typeof winner.coverFile === "string" &&
        winner.coverFile.startsWith(id + "-") &&
        /^[A-Za-z0-9._-]+\.jpg$/u.test(winner.coverFile);
      if (valid) {
        try {
          const file = join(directory, winner.coverFile);
          const info = await stat(file);
          valid =
            info.isFile() &&
            info.size >= 4 &&
            info.size <= cd.CD_LOOKUP_COVER_BYTES;
          if (valid) {
            const bytes = await readFile(file, { signal });
            checkSignal(signal);
            monitor.storageSuccess("cache-cover-read");
            const dimensions = coverDimensions(bytes);
            valid = Boolean(
              dimensions &&
              dimensions.width <= cd.CD_LOOKUP_COVER_SIDE &&
              dimensions.height <= cd.CD_LOOKUP_COVER_SIDE,
            );
          }
        } catch (error) {
          checkSignal(signal, error);
          if (error.code && error.code !== "ENOENT")
            monitor.storageFailure("cache-cover-read", error);
          valid = false;
        }
      }
      if (!valid) {
        delete winner.coverFile;
        winner.coverAvailable = true;
        report.artworkRetryable = true;
      }
    }
    if (
      !Number.isSafeInteger(report.artworkRetryAt) ||
      report.artworkRetryAt < 0
    )
      report.artworkRetryAt = 0;
    return report;
  } catch (error) {
    checkSignal(signal, error);
    if (
      error.code &&
      error.code !== "ENOENT" &&
      error.code !== "CD_REPORT_LIMIT"
    )
      monitor.storageFailure("cache-read", error);
    // Missing, truncated or incompatible cache entries are ordinary misses.
    return null;
  }
}
