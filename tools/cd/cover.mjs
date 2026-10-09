import { matchingArtists, normalizeName } from "./matching.mjs";
import { prepareCover } from "./image.mjs";
import { verifyDiscogs } from "./discogs.mjs";
import { rankCandidates } from "./ranking.mjs";
import { writeFile } from "node:fs/promises";
import { join } from "node:path";
import { request } from "./http.mjs";

// Bound original downloads before FFmpeg resizes them for the console.
const SOURCE_COVER_BYTES = 5 * 1024 * 1024;

export async function downloadCover(
  id,
  candidates,
  selected,
  { userAgent, directory },
) {
  if (selected) {
    for (const candidate of candidates.filter(
      (item) => item.id === selected.releaseId,
    )) {
      if (!candidate.coverUrl || candidate.coverAvailable === false) continue;
      delete candidate.coverError;
      delete candidate.coverRetryAt;
      candidate.coverRetryable = false;
      const rejected = new Set(candidate.coverRejectedUrls ?? []);
      const urls = new Set([
        candidate.coverUrl,
        ...(candidate.coverUrls ?? []),
      ]);
      for (const url of urls) {
        if (rejected.has(url)) continue;
        try {
          const cover = await request(
            url,
            userAgent,
            undefined,
            SOURCE_COVER_BYTES,
          );
          if (!cover) {
            rejected.add(url);
            continue;
          }
          if (
            !cover.type.includes("image/jpeg") &&
            !cover.type.includes("image/png")
          ) {
            const error = Error(`Unexpected cover type: ${cover.type}`);
            error.retryable = false;
            throw error;
          }
          const jpeg = await prepareCover(cover.bytes);
          const filename = `${id}-${candidate.id}.jpg`;
          await writeFile(join(directory, filename), jpeg);
          candidate.coverFile = filename;
          candidate.coverUrl = url;
          candidate.coverAvailable = true;
          candidate.coverRetryable = false;
          delete candidate.coverError;
          delete candidate.coverRetryAt;
          break;
        } catch (error) {
          candidate.coverError = error.message;
          if (error.retryable === false) rejected.add(url);
          else {
            candidate.coverRetryable = true;
            if (error.retryAt)
              candidate.coverRetryAt = Math.max(
                candidate.coverRetryAt ?? 0,
                error.retryAt,
              );
          }
        }
      }
      candidate.coverRejectedUrls = [...rejected];
      if (!candidate.coverFile)
        candidate.coverAvailable = candidate.coverRetryable;
    }
  }
}

// Case and accent differences do not identify a different album. Keep punctuation
// and word order significant so unrelated catalog candidates are not borrowed.
function sameLabel(a, b) {
  return (
    typeof a === "string" &&
    typeof b === "string" &&
    a.trim() &&
    a.trim().normalize("NFC").localeCompare(b.trim().normalize("NFC"), "en", {
      sensitivity: "base",
    }) === 0
  );
}

function adoptCover(winner, candidate, provider, matchBasis) {
  winner.coverFile = candidate.coverFile;
  winner.coverUrl = candidate.coverUrl;
  winner.coverAvailable = true;
  winner.coverSource = {
    provider,
    releaseId: candidate.id,
    url: candidate.url,
    matchBasis,
  };
  delete winner.coverError;
}

/** Borrow only artwork; recognition selection and track metadata stay intact. */
async function existingCover(report, winner, options) {
  if (winner.coverFile) return;
  const medium = winner.media.find(
    (item) => item.position === report.selected.mediumPosition,
  );
  if (!medium?.tracks.length) return;
  for (const candidate of report.candidates) {
    if (
      candidate.id === winner.id ||
      candidate.source !== "musicbrainz" ||
      !candidate.coverUrl ||
      candidate.coverAvailable === false ||
      !matchingArtists(winner, candidate) ||
      !sameLabel(candidate.title, winner.title) ||
      !candidate.media.some(
        (other) =>
          other.tracks.length === medium.tracks.length &&
          other.tracks.every((track, i) =>
            sameLabel(track.title, medium.tracks[i].title),
          ),
      )
    )
      continue;
    await downloadCover(
      report.discId,
      [candidate],
      { releaseId: candidate.id },
      options,
    );
    if (!candidate.coverFile) continue;
    adoptCover(
      winner,
      candidate,
      "musicbrainz",
      "artist-album-all-track-names",
    );
    return;
  }
}

// Artwork may tolerate one catalog typo once the album has been recognized.
// Require a long title and at most one inserted, deleted or substituted character.
function singleTitleTypo(a, b) {
  const left = [...normalizeName(a)],
    right = [...normalizeName(b)];
  if (
    Math.min(left.length, right.length) < 12 ||
    Math.abs(left.length - right.length) > 1
  )
    return false;
  let i = 0,
    j = 0,
    edits = 0;
  while (i < left.length && j < right.length) {
    if (left[i] === right[j]) {
      ++i;
      ++j;
      continue;
    }
    if (++edits > 1) return false;
    if (left.length >= right.length) ++i;
    if (right.length >= left.length) ++j;
  }
  return edits + (left.length - i) + (right.length - j) === 1;
}

function isolatedTitleTypo(candidate, medium) {
  const tracks = candidate.media.find(
    (item) => item.position === candidate.comparison.mediumPosition,
  )?.tracks;
  if (
    !tracks ||
    tracks.length < 10 ||
    tracks.length !== medium.tracks.length ||
    candidate.verification.agreeingTrackNames !== tracks.length - 1
  )
    return false;
  const differences = tracks.filter(
    (track, i) =>
      normalizeName(track.title) !== normalizeName(medium.tracks[i].title),
  );
  return (
    differences.length === 1 &&
    tracks.some((track, i) =>
      singleTitleTypo(track.title, medium.tracks[i].title),
    )
  );
}

/** Missing artwork never invalidates an accepted metadata match. */
export async function fallbackCover(report, options) {
  if (!report.selected) return;
  const winner = report.candidates.find(
    (item) => item.id === report.selected.releaseId,
  );
  if (!winner || winner.coverFile) return;
  const medium = winner.media.find(
    (item) => item.position === report.selected.mediumPosition,
  );
  if (!medium) return;
  try {
    const result = await verifyDiscogs(
      [
        {
          artist: winner.artist,
          artists: winner.artists,
          title: winner.title,
          source: winner.source ?? "musicbrainz",
          id: winner.id,
          tracks: medium.tracks.map((track) => track.title),
        },
      ],
      options,
      (report.coverFallback?.raw ?? report.discogs?.raw ?? []).filter(
        (item) => !report.coverFallback || !item.error,
      ),
    );
    rankCandidates(report.toc, result.candidates);
    const previous = report.coverFallback?.candidates ?? [];
    for (const candidate of result.candidates) {
      const retained = previous.find((item) => item.id === candidate.id);
      if (retained) {
        // Retain permanent failures per URL, allowing other images of this release.
        candidate.coverRejectedUrls =
          retained.coverRejectedUrls ??
          (retained.coverAvailable === false ? [retained.coverUrl] : []);
      }
    }
    report.coverFallback = {
      candidates: result.candidates,
      raw: result.raw,
      errors: result.errors,
    };
    for (const candidate of result.candidates) {
      // The album is already identified. Another edition may supply artwork
      // despite missing/different timings, with matching names or one small typo.
      const allTrackNamesMatch =
        candidate.verification.trackCount > 0 &&
        candidate.verification.agreeingTrackNames ===
          candidate.verification.trackCount;
      const titleTypo = isolatedTitleTypo(candidate, medium);
      if (
        candidate.id === winner.id ||
        !candidate.coverUrl ||
        (candidate.comparison.rejection && !allTrackNamesMatch && !titleTypo)
      )
        continue;
      await downloadCover(
        report.discId,
        [candidate],
        { releaseId: candidate.id },
        options,
      );
      if (!candidate.coverFile) continue;
      adoptCover(
        winner,
        candidate,
        "discogs",
        candidate.comparison.rejection
          ? titleTypo
            ? "artist-album-track-names-isolated-typo"
            : "artist-album-all-track-names"
          : "artist-album-track-names-and-timings",
      );
      return;
    }
  } catch (error) {
    report.coverFallback = {
      error: error.message,
      retryable: error.retryable !== false,
      ...(error.retryAt ? { retryAt: error.retryAt } : {}),
    };
  }
}

/** Retry artwork independently of metadata; a null directory disables storage and artwork. */
export async function resolveCover(report, options) {
  report.artworkRetryAt = 0;
  if (options.directory === null) {
    report.artworkRetryable = false;
    return;
  }
  if (!report.selected) return;
  const winner = report.candidates.find(
    (item) => item.id === report.selected.releaseId,
  );
  if (!winner || winner.coverFile) {
    report.artworkRetryable = false;
    return;
  }
  await downloadCover(
    report.discId,
    report.candidates,
    report.selected,
    options,
  );
  await existingCover(report, winner, options);
  await fallbackCover(report, options);
  report.artworkRetryable =
    !winner.coverFile &&
    Boolean(
      report.candidates.some((candidate) => candidate.coverRetryable) ||
      report.coverFallback?.retryable ||
      report.coverFallback?.errors?.some((error) => error.retryable) ||
      report.coverFallback?.candidates?.some(
        (candidate) => candidate.coverRetryable,
      ),
    );
  if (report.artworkRetryable)
    report.artworkRetryAt = Math.max(
      0,
      ...report.candidates.map((candidate) => candidate.coverRetryAt ?? 0),
      report.coverFallback?.retryAt ?? 0,
      ...(report.coverFallback?.errors ?? []).map(
        (error) => error.retryAt ?? 0,
      ),
      ...(report.coverFallback?.candidates ?? []).map(
        (candidate) => candidate.coverRetryAt ?? 0,
      ),
    );
}
