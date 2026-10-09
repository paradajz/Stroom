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
import { checkReport } from "./report.mjs";
import { checkSignal } from "./deadline.mjs";

/** MusicBrainz, then GnuDB, then CTDB verified against Discogs. */
export async function lookupDisc(toc, options) {
  const report = await selectDisc(toc, options);
  if (report.selected) await options.onSelected?.(report);
  await resolveCover(report, options);
  return report;
}

/** Recognize metadata without waiting for artwork. */
export async function selectDisc(toc, options) {
  checkSignal(options.signal);
  let report;
  let retryAt = 0;
  const failed = (source, error) => {
    checkSignal(options.signal, {
      retryAt: Math.max(retryAt, error.retryAt ?? 0),
    });
    if (error.code === "CD_REPORT_LIMIT") throw error;
    report.errors = { ...report.errors, [source]: error.message };
    if (Number.isSafeInteger(error.retryAt))
      retryAt = Math.max(retryAt, error.retryAt);
  };
  try {
    report = await lookupMusicBrainz(toc, options);
    checkReport(report);
    checkSignal(options.signal);
  } catch (error) {
    report = {
      discId: discId(toc),
      toc,
      lookedUpAt: new Date().toISOString(),
      match: "none",
      selected: null,
      candidates: [],
      candidateCount: 0,
      fuzzyLimits: {
        meanDifferenceMs: FUZZY_MAX_MEAN_MS,
        maxDifferenceMs: FUZZY_MAX_TRACK_MS,
      },
    };
    failed("musicbrainz", error);
  }
  if (report.selected) {
    return report;
  }
  try {
    const gnudb = await lookupGnudb(toc, options);
    report.gnudb = gnudb;
    report.candidates.push(...gnudb.candidates);
    report.candidateCount = report.candidates.length;
    checkReport(report);
    report.selected = rankCandidates(toc, report.candidates);
    checkSignal(options.signal, { retryAt });
    if (report.selected) {
      report.match = "gnudb";
      report.note = "GnuDB track timings agree; edition unconfirmed";
      return report;
    }
    if (gnudb.errors?.length) {
      const error = Error(
        gnudb.errors.map((error) => error.message).join("; "),
      );
      error.retryAt = Math.max(
        0,
        ...gnudb.errors.map((item) => item.retryAt ?? 0),
      );
      throw error;
    }
  } catch (error) {
    failed("gnudb", error);
  }
  try {
    const ctdb = await lookupCtdb(toc, options);
    report.ctdb = ctdb;
    checkReport(report);
    const verified = await verifyDiscogs(ctdb.candidates, options);
    report.discogs = { raw: verified.raw, errors: verified.errors };
    report.candidates.push(...verified.candidates);
    report.candidateCount = report.candidates.length;
    checkReport(report);
    report.selected = rankCandidates(toc, report.candidates);
    checkSignal(options.signal, { retryAt });
    if (report.selected) {
      report.match = "ctdb-discogs";
      report.note = report.selected.isolatedCatalogError
        ? "CTDB/Discogs match; isolated catalog timing discrepancy on track " +
          report.selected.timingOutliers.join(", ") +
          "; edition unconfirmed"
        : "CTDB/Discogs track names and durations agree; edition unconfirmed";
      return report;
    }
    if (verified.errors.length) {
      const error = Error(
        verified.errors.map((error) => error.message).join("; "),
      );
      error.retryAt = Math.max(
        0,
        ...verified.errors.map((item) => item.retryAt ?? 0),
      );
      throw error;
    }
  } catch (error) {
    failed("ctdbDiscogs", error);
  }
  if (report.errors && Object.keys(report.errors).length) {
    const error = Error(
      Object.entries(report.errors)
        .map(([source, message]) => source + ": " + message)
        .join("; "),
    );
    error.retryAt = retryAt;
    throw error;
  }
  report.note =
    "No acceptable MusicBrainz, GnuDB or CTDB/Discogs match; nothing sent to PS2";
  return report;
}
