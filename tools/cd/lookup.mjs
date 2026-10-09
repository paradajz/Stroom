import { lookupDisc as lookupMusicBrainz } from "./musicbrainz.mjs";
import { lookupGnudb } from "./gnudb.mjs";
import { lookupCtdb } from "./ctdb.mjs";
import { verifyDiscogs } from "./discogs.mjs";
import {
  rankCandidates,
  FUZZY_MAX_MEAN_MS,
  FUZZY_MAX_TRACK_MS,
} from "./ranking.mjs";
import { resolveCover } from "./cover.mjs";
import { discId } from "./disc.mjs";

/** MusicBrainz, then GnuDB, then CTDB verified against Discogs. */
export async function lookupDisc(toc, options) {
  const report = await selectDisc(toc, options);
  if (report.selected) await options.onSelected?.(report);
  await resolveCover(report, options);
  return report;
}

/** Recognize metadata without waiting for artwork. */
export async function selectDisc(toc, options) {
  let report;
  try {
    report = await lookupMusicBrainz(toc, options);
  } catch (error) {
    report = {
      discId: discId(toc),
      toc,
      lookedUpAt: new Date().toISOString(),
      match: "none",
      selected: null,
      candidates: [],
      candidateCount: 0,
      errors: { musicbrainz: error.message },
      fuzzyLimits: {
        meanDifferenceMs: FUZZY_MAX_MEAN_MS,
        maxDifferenceMs: FUZZY_MAX_TRACK_MS,
      },
    };
  }
  if (report.selected) {
    return report;
  }
  try {
    const gnudb = await lookupGnudb(toc, options);
    report.gnudb = gnudb;
    report.candidates.push(...gnudb.candidates);
    report.candidateCount = report.candidates.length;
    report.selected = rankCandidates(toc, report.candidates);
    if (report.selected) {
      report.match = "gnudb";
      report.note = "GnuDB track timings agree; edition unconfirmed";
      return report;
    }
    if (gnudb.errors?.length)
      throw Error(gnudb.errors.map((error) => error.message).join("; "));
  } catch (error) {
    report.errors = { ...report.errors, gnudb: error.message };
  }
  try {
    const ctdb = await lookupCtdb(toc, options);
    report.ctdb = ctdb;
    const verified = await verifyDiscogs(ctdb.candidates, options);
    report.discogs = { raw: verified.raw, errors: verified.errors };
    report.candidates.push(...verified.candidates);
    report.candidateCount = report.candidates.length;
    report.selected = rankCandidates(toc, report.candidates);
    if (report.selected) {
      report.match = "ctdb-discogs";
      report.note = report.selected.isolatedCatalogError
        ? "CTDB/Discogs match; isolated catalog timing discrepancy on track " +
          report.selected.timingOutliers.join(", ") +
          "; edition unconfirmed"
        : "CTDB/Discogs track names and durations agree; edition unconfirmed";
      return report;
    }
    if (verified.errors.length)
      throw Error(verified.errors.map((error) => error.message).join("; "));
  } catch (error) {
    report.errors = { ...report.errors, ctdbDiscogs: error.message };
  }
  if (report.errors && Object.keys(report.errors).length)
    throw Error(
      Object.entries(report.errors)
        .map(([source, message]) => source + ": " + message)
        .join("; "),
    );
  report.note =
    "No acceptable MusicBrainz, GnuDB or CTDB/Discogs match; nothing sent to PS2";
  return report;
}
