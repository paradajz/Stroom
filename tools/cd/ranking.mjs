import { cd } from "../contracts/load.mjs";

// Conservative fuzzy-match limits: tolerate small mastering/pregap differences,
// but never turn the least-bad unrelated album into an automatic match.
// Exact Disc IDs are stronger evidence and may have incomplete catalog timings.
export const FUZZY_MAX_MEAN_MS = 2000;
export const FUZZY_MAX_TRACK_MS = 5000;

// A corroborated album may contain one catalog typo, but at least 90% of
// ten or more tracks must agree within three seconds; never discard timings.
const OUTLIER_MAX_MS = 30000;
const CORROBORATED_TRACK_MS = 3000;
const CORROBORATED_MIN_TRACKS = 10;

function rejection(score) {
  if (score.trackCountDifference) return "Track count differs";
  if (score.exactDisc) return null;
  if (score.missingDurations) return "Incomplete track timings";
  if (score.isolatedCatalogError) return null;
  if (
    score.meanDifferenceMs > FUZZY_MAX_MEAN_MS ||
    score.maxDifferenceMs > FUZZY_MAX_TRACK_MS
  )
    return "Track durations exceed fuzzy-match limits";
  return null;
}

function compare(a, b) {
  return (
    Number(Boolean(rejection(a))) - Number(Boolean(rejection(b))) ||
    a.trackCountDifference - b.trackCountDifference ||
    Number(b.exactDisc) - Number(a.exactDisc) ||
    a.missingDurations - b.missingDurations ||
    (a.meanDifferenceMs ?? Infinity) - (b.meanDifferenceMs ?? Infinity) ||
    (a.maxDifferenceMs ?? Infinity) - (b.maxDifferenceMs ?? Infinity) ||
    0
  );
}

/** Compare each release's closest medium, retaining evidence and deterministic ties. */
export function rankCandidates(toc, candidates) {
  const lengths = toc.offsets.map(
    (offset, i) =>
      (((toc.offsets[i + 1] ?? toc.leadout) - offset) * 1000) /
      cd.CD_SECTORS_PER_SECOND,
  );
  for (const candidate of candidates) {
    const scores = candidate.media.map((medium) => {
      const differencesMs = lengths.map((length, i) => {
        const value = medium.tracks[i]?.milliseconds;
        return Number.isFinite(value) && value > 0
          ? Math.abs(value - length)
          : null;
      });
      const known = differencesMs.filter((value) => value !== null);
      const outliers = differencesMs.flatMap((value, i) =>
        value > CORROBORATED_TRACK_MS ? [i + 1] : [],
      );
      const inliers = known.filter((value) => value <= CORROBORATED_TRACK_MS);
      const isolatedCatalogError = Boolean(
        medium.corroboratedNames &&
        known.length === lengths.length &&
        lengths.length >= CORROBORATED_MIN_TRACKS &&
        outliers.length === 1 &&
        inliers.length / lengths.length >= 0.9 &&
        Math.max(...known) <= OUTLIER_MAX_MS &&
        inliers.reduce((a, b) => a + b, 0) / inliers.length <=
          FUZZY_MAX_MEAN_MS,
      );
      return {
        isolatedCatalogError,
        timingOutliers: isolatedCatalogError ? outliers : [],
        mediumPosition: medium.position,
        exactDisc: medium.exactDisc,
        trackCountDifference: Math.abs(medium.tracks.length - lengths.length),
        missingDurations: lengths.length - known.length,
        meanDifferenceMs: known.length
          ? known.reduce((a, b) => a + b, 0) / known.length
          : null,
        maxDifferenceMs: known.length ? Math.max(...known) : null,
        differencesMs,
      };
    });
    scores.sort((a, b) => compare(a, b) || a.mediumPosition - b.mediumPosition);
    candidate.comparison = scores[0] ?? {
      mediumPosition: null,
      exactDisc: false,
      trackCountDifference: lengths.length,
      missingDurations: lengths.length,
      meanDifferenceMs: null,
      maxDifferenceMs: null,
      differencesMs: [],
    };
  }
  for (const candidate of candidates)
    candidate.comparison.rejection = rejection(candidate.comparison);
  candidates.sort(
    (a, b) =>
      compare(a.comparison, b.comparison) ||
      String(a.id).localeCompare(String(b.id), "en"),
  );
  const best = candidates[0];
  if (!best || best.comparison.rejection) return null;
  return {
    releaseId: best.id,
    ...best.comparison,
    tiedCandidates: candidates.filter(
      (candidate) => compare(best.comparison, candidate.comparison) === 0,
    ).length,
  };
}
