import fs from "node:fs";
import path from "node:path";
import { createHash, randomUUID } from "node:crypto";
import { display, milkdrop } from "../contracts/load.mjs";
import { markQuickPresets } from "./select_quick_presets.mjs";

/** Preserve the playback FPS floor while deriving inclusion from measurements. */
export function selectPlaybackPresets(report) {
  return report.results
    .filter(
      (result) =>
        result.classification === "okay" &&
        result.steady_fps >= milkdrop.MILKDROP_FPS_BASELINE_MINIMUM,
    )
    .map((result) => result.file);
}

/** Only complete, unprofiled measurements can drive preset selection. */
export function validateBenchmark(report) {
  const configuration = report.configuration;
  const refreshHz =
    display.DISPLAY_REFRESH_NUMERATOR / display.DISPLAY_REFRESH_DENOMINATOR;
  if (
    report.status !== "complete" ||
    report.error ||
    report.current !== null ||
    !Array.isArray(report.results) ||
    !report.results.length ||
    configuration?.total !== report.results.length ||
    ["render_profile", "detail_profile", "wave_profile"].some(
      (key) => configuration[key] !== 0,
    ) ||
    configuration.video_mode !== display.DISPLAY_MODE_NAME ||
    !Number.isFinite(configuration.refresh_hz) ||
    Math.abs(configuration.refresh_hz - refreshHz) > 0.000001
  )
    throw Error(
      "Preset selection requires a complete full benchmark matching the display profile, with detailed profiling disabled",
    );
  const files = new Set();
  for (const result of report.results) {
    if (
      typeof result.file !== "string" ||
      !result.file ||
      path.isAbsolute(result.file) ||
      result.file.split(/[\\/]/).includes("..") ||
      files.has(result.file) ||
      typeof result.sha256 !== "string" ||
      !/^[a-f0-9]{64}$/.test(result.sha256) ||
      !Number.isFinite(result.work_avg_us) ||
      result.work_avg_us < 0 ||
      !Number.isFinite(result.steady_fps) ||
      result.steady_fps < 0 ||
      !["okay", "borderline", "slow"].includes(result.classification) ||
      ![true, false, null].includes(result.quick)
    )
      throw Error("Invalid benchmark preset measurement");
    files.add(result.file);
  }
  const quickCount = report.results.filter(
    (result) => result.quick === true,
  ).length;
  if (
    report.results.length >= 9
      ? quickCount !== 9 ||
        report.results.some((result) => result.quick === null)
      : report.results.some((result) => result.quick !== null)
  )
    throw Error(
      "Benchmark must mark nine quick presets, or null for libraries smaller than nine",
    );
}

/** Timestamped filenames define latest consistently across checkouts. */
export function loadLatestBenchmark(directory) {
  const latest = fs.existsSync(directory)
    ? fs
        .readdirSync(directory, { withFileTypes: true })
        .filter((entry) => entry.isFile() && entry.name.endsWith(".json"))
        .map((entry) => entry.name)
        .sort()
        .at(-1)
    : null;
  if (!latest) throw Error(`No benchmark report found in ${directory}`);
  const filename = path.resolve(directory, latest);
  const report = JSON.parse(fs.readFileSync(filename, "utf8"));
  validateBenchmark(report);
  return { filename, report };
}

/** Publish a verified full sweep so the next build discovers it directly. */
export function archiveBenchmark(compilerReport, run, reportName) {
  if (compilerReport.selection !== "all-compatible") return null;
  markQuickPresets(run);
  validateBenchmark(run);
  if (
    typeof compilerReport.preset_root !== "string" ||
    typeof compilerReport.benchmark_dir !== "string" ||
    typeof reportName !== "string" ||
    path.basename(reportName) !== reportName ||
    !reportName.endsWith(".json")
  )
    throw Error("Invalid benchmark archive paths");
  const entries = compilerReport.entries.filter(
    (entry) => entry.status === "INCLUDED",
  );
  if (entries.length !== run.results.length)
    throw Error("Full benchmark coverage does not match the compiled library");
  entries.forEach((entry, index) => {
    const result = run.results[index];
    if (
      result.index !== index ||
      result.file !== entry.file ||
      result.sha256 !== entry.sha256
    )
      throw Error(
        "Full benchmark coverage does not match the compiled library",
      );
    const hash = createHash("sha256")
      .update(
        fs.readFileSync(path.join(compilerReport.preset_root, result.file)),
      )
      .digest("hex");
    if (hash !== result.sha256)
      throw Error(`Preset changed since the benchmark build: ${result.file}`);
  });
  const archive = path.join(compilerReport.benchmark_dir, reportName);
  const temporary = archive + `.${randomUUID()}.tmp`;
  fs.mkdirSync(path.dirname(archive), { recursive: true });
  try {
    fs.writeFileSync(temporary, JSON.stringify(run, null, 2) + "\n", {
      flag: "wx",
    });
    // Refuse to overwrite an existing run, and expose only the complete file.
    fs.linkSync(temporary, archive);
  } finally {
    fs.rmSync(temporary, { force: true });
  }
  return archive;
}
