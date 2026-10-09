import path from "node:path";

/** Rank all measured presets by mean work, preserving three distinct groups. */
export function selectQuickPresets(report) {
  if (
    report.status !== "complete" ||
    report.error ||
    report.current !== null ||
    !Array.isArray(report.results) ||
    report.results.length < 9 ||
    report.configuration?.total !== report.results.length ||
    ["render_profile", "detail_profile", "wave_profile"].some(
      (key) => report.configuration[key] !== 0,
    )
  )
    throw Error(
      "Quick selection requires a complete full benchmark with at least nine presets and detailed profiling disabled",
    );
  const files = new Set();
  for (const result of report.results) {
    if (
      typeof result.file !== "string" ||
      !result.file ||
      path.isAbsolute(result.file) ||
      result.file.split(/[\\/]/).includes("..") ||
      files.has(result.file) ||
      !Number.isFinite(result.work_avg_us) ||
      result.work_avg_us < 0
    )
      throw Error(
        "Quick selection requires unique preset paths and valid mean work times",
      );
    files.add(result.file);
  }
  const ranked = [...report.results].sort(
    (a, b) =>
      b.work_avg_us - a.work_avg_us ||
      (a.file < b.file ? -1 : a.file > b.file ? 1 : 0),
  );
  // For even counts, center the middle group on the upper middle index.
  const middle = Math.floor(ranked.length / 2) - 1;
  return [
    ...ranked.slice(0, 3),
    ...ranked.slice(middle, middle + 3),
    ...ranked.slice(-3),
  ].map((result) => result.file);
}

/** Store quick membership in the measurements themselves. */
export function markQuickPresets(report) {
  const complete =
    report.status === "complete" && !report.error && report.current === null;
  const unprofiled = ["render_profile", "detail_profile", "wave_profile"].every(
    (key) => report.configuration?.[key] === 0,
  );
  const selected =
    complete && unprofiled && report.results.length >= 9
      ? new Set(selectQuickPresets(report))
      : null;
  for (const result of report.results)
    result.quick = selected ? selected.has(result.file) : null;
}
