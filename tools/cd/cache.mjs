import { homedir } from "node:os";
import { isAbsolute, join } from "node:path";
import { readFile, writeFile, rename, stat } from "node:fs/promises";
import { discId } from "./disc.mjs";
import { artwork } from "../contracts/load.mjs";
const { ARTWORK_MAX_BYTES } = artwork;

const CACHE_VERSION = 1;

export function cacheDirectory(
  platform = process.platform,
  home = homedir(),
  xdg = process.env.XDG_CACHE_HOME,
) {
  const base =
    xdg && isAbsolute(xdg)
      ? xdg
      : platform === "darwin"
        ? join(home, "Library", "Caches")
        : join(home, ".cache");
  return join(base, "stroom", "cd");
}

export async function saveReport(directory, id, report) {
  const path = join(directory, id + ".json");
  await writeFile(
    path + ".tmp",
    JSON.stringify({ ...report, cacheVersion: CACHE_VERSION }, null, 2) + "\n",
  );
  await rename(path + ".tmp", path);
}

/** Reuse only complete recognition; missing artwork never discards valid labels. */
export async function cachedReport(directory, id) {
  try {
    const report = JSON.parse(
      await readFile(join(directory, id + ".json"), "utf8"),
    );
    if (
      report.cacheVersion !== CACHE_VERSION ||
      report.discId !== id ||
      discId(report.toc) !== id ||
      !report.selected ||
      !Array.isArray(report.candidates)
    )
      return null;
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
            info.isFile() && info.size >= 4 && info.size <= ARTWORK_MAX_BYTES;
          if (valid) {
            const bytes = await readFile(file);
            valid =
              bytes.readUInt16BE(0) === 0xffd8 &&
              bytes.readUInt16BE(bytes.length - 2) === 0xffd9;
          }
        } catch {
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
  } catch {
    // Missing, truncated or incompatible cache entries are ordinary misses.
    return null;
  }
}
