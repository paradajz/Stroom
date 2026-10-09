import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createHash } from "node:crypto";
import { generate } from "../../../tools/milkdrop/compile_presets.mjs";
import {
  archiveBenchmark,
  loadLatestBenchmark,
} from "../../../tools/milkdrop/benchmark_reports.mjs";
import {
  markQuickPresets,
  selectQuickPresets,
} from "../../../tools/milkdrop/select_quick_presets.mjs";

const directory = fs.mkdtempSync(path.join(os.tmpdir(), "stroom-reports-"));
let serial = 0;
function fixture(count = 12) {
  const base = path.join(directory, String(serial++));
  const root = path.join(base, "source");
  const benchmarks = path.join(base, "benchmark");
  fs.mkdirSync(root, { recursive: true });
  fs.mkdirSync(benchmarks);
  const results = Array.from({ length: count }, (_, index) => {
    const file = `preset-${String(index).padStart(2, "0")}.milk`;
    const source = "per_frame_1=wave_r=.5;";
    fs.writeFileSync(path.join(root, file), source);
    return {
      index,
      file,
      sha256: createHash("sha256").update(source).digest("hex"),
      steady_fps: index === 0 ? 29.97 : 59.94,
      work_avg_us: (index + 1) * 1000,
      classification: index < 2 ? "okay" : index === 2 ? "borderline" : "slow",
    };
  });
  const run = {
    status: "complete",
    current: null,
    configuration: {
      total: count,
      render_profile: 0,
      detail_profile: 0,
      wave_profile: 0,
      video_mode: "480p",
      refresh_hz: 59.94006,
    },
    results,
  };
  markQuickPresets(run);
  const baseline = path.join(benchmarks, "2026-01-01.json");
  fs.writeFileSync(baseline, JSON.stringify(run));
  generate(root, benchmarks, path.join(base, "full"), "all-compatible");
  const compiler = JSON.parse(
    fs.readFileSync(path.join(base, "full/preset-report.json")),
  );
  return { base, root, benchmarks, baseline, run, compiler };
}
const included = (base) =>
  JSON.parse(
    fs.readFileSync(path.join(base, "preset-report.json")),
  ).entries.filter((entry) => entry.status === "INCLUDED");

try {
  const full = fixture();
  const previous = fs.readFileSync(full.baseline, "utf8");
  const archive = archiveBenchmark(full.compiler, full.run, "2026-01-02.json");
  assert.equal(fs.readFileSync(full.baseline, "utf8"), previous);
  assert.equal(loadLatestBenchmark(full.benchmarks).filename, archive);
  assert.deepEqual(JSON.parse(fs.readFileSync(archive)), full.run);
  generate(full.root, full.benchmarks, path.join(full.base, "playback"));
  assert.deepEqual(
    included(path.join(full.base, "playback")).map((result) => result.file),
    full.run.results.slice(0, 2).map((result) => result.file),
  );
  assert.deepEqual(
    included(path.join(full.base, "playback")).map(
      (result) => result.benchmark_fps,
    ),
    [29.97, 59.94],
  );
  generate(full.root, full.benchmarks, path.join(full.base, "quick"), "quick");
  assert.deepEqual(
    included(path.join(full.base, "quick"))
      .map((result) => result.file)
      .sort(),
    selectQuickPresets(full.run).sort(),
  );
  assert.ok(
    included(path.join(full.base, "quick")).some(
      (result) =>
        full.run.results.find((measurement) => measurement.file === result.file)
          .classification !== "okay",
    ),
  );

  // New okay measurements become playable automatically; no approval list remains.
  full.run.results[2].classification = "okay";
  archiveBenchmark(full.compiler, full.run, "2026-01-03.json");
  // Filename ordering survives checkouts and does not depend on file modification times.
  fs.utimesSync(full.baseline, new Date(), new Date());
  generate(full.root, full.benchmarks, path.join(full.base, "playback"));
  assert.equal(included(path.join(full.base, "playback")).length, 3);
  full.run.results[0].steady_fps = 29.96;
  archiveBenchmark(full.compiler, full.run, "2026-01-03T01.json");
  generate(full.root, full.benchmarks, path.join(full.base, "playback"));
  assert.equal(included(path.join(full.base, "playback")).length, 2);
  assert.ok(
    !included(path.join(full.base, "playback")).some(
      (entry) => entry.file === full.run.results[0].file,
    ),
  );
  assert.throws(
    () => archiveBenchmark(full.compiler, full.run, "2026-01-03.json"),
    /EEXIST/,
  );

  const small = fixture(4);
  assert.ok(small.run.results.every((result) => result.quick === null));
  archiveBenchmark(small.compiler, small.run, "2026-01-02.json");
  assert.throws(
    () =>
      generate(
        small.root,
        small.benchmarks,
        path.join(small.base, "quick"),
        "quick",
      ),
    /at least nine/,
  );

  const quick = fixture();
  quick.compiler.selection = "quick";
  assert.equal(archiveBenchmark(quick.compiler, quick.run, "quick.json"), null);
  assert.deepEqual(fs.readdirSync(quick.benchmarks), [
    path.basename(quick.baseline),
  ]);

  for (const change of [
    ({ run }) => {
      run.status = "interrupted";
    },
    ({ run }) => {
      run.current = 1;
    },
    ({ run }) => {
      run.error = "Invalid run";
    },
    ({ run }) => {
      run.results.pop();
    },
    ({ run }) => {
      run.configuration.total--;
    },
    ({ run }) => {
      run.configuration.render_profile = 1;
    },
    ({ run }) => {
      run.configuration.detail_profile = 1;
    },
    ({ run }) => {
      run.configuration.wave_profile = 1;
    },
    ({ run }) => {
      run.configuration.refresh_hz = 50;
    },
    ({ run }) => {
      run.configuration.video_mode = "PAL";
    },
    ({ run }) => {
      run.results[0].file = "../outside.milk";
    },
    ({ run }) => {
      run.results[0].sha256 = "0".repeat(64);
    },
    ({ run }) => {
      run.results[0].steady_fps = NaN;
    },
    ({ run }) => {
      run.results[0].work_avg_us = NaN;
    },
    ({ run }) => {
      run.results[0].classification = "invalid";
    },
    ({ root, run }) => {
      fs.appendFileSync(
        path.join(root, run.results[0].file),
        "\nper_frame_2=wave_g=1;",
      );
    },
  ]) {
    const failed = fixture();
    change(failed);
    assert.throws(() =>
      archiveBenchmark(failed.compiler, failed.run, "2026-01-02.json"),
    );
    assert.deepEqual(fs.readdirSync(failed.benchmarks), [
      path.basename(failed.baseline),
    ]);
  }

  const missing = path.join(directory, "missing");
  for (const mode of ["playback", "quick", "all-compatible"])
    assert.throws(
      () =>
        generate(
          full.root,
          missing,
          path.join(directory, "missing-output"),
          mode,
        ),
      /No benchmark report found/,
    );
  fs.mkdirSync(missing);
  assert.throws(
    () => loadLatestBenchmark(missing),
    /No benchmark report found/,
  );

  // A broken newest report must fail instead of silently falling back to an older one.
  for (const change of [
    (run) => {
      run.status = "incomplete";
    },
    (run) => {
      run.results[0].quick = "yes";
    },
    (run) => {
      run.results[0].quick = null;
    },
    (run) => {
      run.results[0].file = run.results[1].file;
    },
  ]) {
    const invalid = structuredClone(full.run);
    change(invalid);
    fs.writeFileSync(
      path.join(full.benchmarks, "2026-01-04.json"),
      JSON.stringify(invalid),
    );
    assert.throws(() => loadLatestBenchmark(full.benchmarks));
  }
  console.log(
    "PASS: latest-report discovery, classification playback, embedded quick selection, atomic full-run archiving and missing/invalid-report failures",
  );
} finally {
  fs.rmSync(directory, { recursive: true, force: true });
}
