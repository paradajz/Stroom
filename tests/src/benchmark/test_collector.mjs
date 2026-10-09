import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { BenchmarkResults } from "../../../tools/milkdrop/benchmark_results.mjs";

const entries = [
  { status: "INCLUDED", file: 'one,"test".milk', sha256: "a" },
  { status: "SKIPPED", file: "skip" },
  { status: "INCLUDED", file: "two.milk", sha256: "b" },
];
const start = {
  event: "start",
  version: 2,
  total: 2,
  audio: "generated-ariacast",
  duration_us: 10000000,
  warmup_us: 1000000,
  budget_us: 20000,
};
const result = {
  event: "result",
  index: 0,
  elapsed_us: 10000000,
  frames: 500,
  warmup_frames: 50,
  first_work_us: 10000,
  preparation_avg_us: 500,
  update_avg_us: 2000,
  render_avg_us: 6000,
  submit_avg_us: 1500,
  samples: 450,
  work_avg_us: 10000,
  work_p95_us: 19000,
  work_max_us: 23000,
  interval_avg_us: 20000,
  interval_p95_us: 20000,
  interval_max_us: 40000,
  steady_fps: 50,
  over_budget: 1,
};
const validation = {
  event: "color_curve_validation",
  mode: 1,
  pixels: 307200,
  mismatches: 0,
  readback: true,
};
const checked = new BenchmarkResults(entries);
// Independent wire fixtures: changing contract values must not silently rewrite expected IDs.
for (const mode of [1, 2, 3, 4, 5, 6, 7]) {
  checked.consume({ ...validation, mode });
}
assert.deepEqual(
  checked.state.color_curve_validations.map((v) => v.mode),
  [1, 2, 3, 4, 5, 6, 7],
);
for (const mode of [0, -1, 8, 1.5, "1", null, NaN, Infinity, 4294967297]) {
  assert.throws(() => checked.consume({ ...validation, mode }), /Invalid/);
}
assert.throws(() => checked.consume({ ...validation, mismatches: -1 }));
checked.consume(start);
assert.throws(() => checked.consume(validation));
const run = new BenchmarkResults(entries);
assert.throws(() => run.consume({ event: "begin", index: 0 }));
run.consume(start);
run.consume({ event: "begin", index: 0 });
assert.throws(() => run.consume({ ...result, samples: 0 }));
run.consume(result);
assert.equal(run.state.results[0].classification, "okay");
assert.match(run.csv(), /one,""test""\.milk/);
assert.throws(() => run.consume(result));
run.consume({ event: "begin", index: 1 });
run.consume({ ...result, index: 1, work_avg_us: 22000, work_p95_us: 30000 });
assert.equal(run.state.results[1].classification, "slow");
run.consume({ event: "complete", total: 2 });
assert.equal(run.state.status, "complete");
const profiled = new BenchmarkResults(entries);
profiled.consume({ ...start, render_profile: 1 });
profiled.consume({ event: "begin", index: 0 });
assert.throws(() => profiled.consume(result), /render profile/);
const profile = {
  ...result,
  mesh_avg_us: 2000,
  feedback_geometry_avg_us: 500,
  feedback_commands_avg_us: 1000,
};
assert.throws(
  () => profiled.consume({ ...profile, mesh_avg_us: -1 }),
  /render profile/,
);
assert.throws(
  () => profiled.consume({ ...profile, feedback_commands_avg_us: 6000 }),
  /render profile/,
);
profiled.consume(profile);
assert.equal(profiled.state.results[0].mesh_avg_us, 2000);
assert.match(profiled.csv(), /feedback_commands_avg_us/);
const detailed = new BenchmarkResults(entries);
detailed.consume({ ...start, render_profile: 1, detail_profile: 1 });
detailed.consume({ event: "begin", index: 0 });
assert.throws(() => detailed.consume(profile), /detailed profile/);
const details = {
  ...profile,
  audio_analysis_avg_us: 1000,
  preset_equations_avg_us: 100,
  custom_wave_update_avg_us: 300,
  custom_shape_update_avg_us: 100,
  wave_geometry_avg_us: 200,
  shape_geometry_avg_us: 100,
  draw_commands_avg_us: 300,
  presentation_avg_us: 50,
};
assert.throws(
  () => detailed.consume({ ...details, custom_wave_update_avg_us: 3000 }),
  /detailed profile/,
);
assert.throws(
  () => detailed.consume({ ...details, draw_commands_avg_us: 6000 }),
  /detailed profile/,
);
assert.throws(
  () => detailed.consume({ ...details, audio_analysis_avg_us: -1 }),
  /detailed profile/,
);
detailed.consume(details);
assert.equal(detailed.state.results[0].custom_wave_update_avg_us, 300);
assert.match(detailed.csv(), /wave_geometry_avg_us/);
const waves = new BenchmarkResults(entries);
waves.consume({
  ...start,
  render_profile: 1,
  detail_profile: 1,
  wave_profile: 1,
});
waves.consume({ event: "begin", index: 0 });
assert.throws(() => waves.consume(details), /wave profile/);
const waveResult = {
  ...details,
  wave_frame_equations_avg_us: 5,
  wave_samples_avg_us: 20,
  wave_point_inputs_avg_us: 10,
  wave_point_equations_avg_us: 220,
  wave_vertex_store_avg_us: 20,
};
assert.throws(
  () => waves.consume({ ...waveResult, wave_point_equations_avg_us: 500 }),
  /wave profile/,
);
assert.throws(
  () => waves.consume({ ...waveResult, wave_vertex_store_avg_us: -1 }),
  /wave profile/,
);
waves.consume(waveResult);
assert.equal(waves.state.results[0].wave_point_equations_avg_us, 220);
assert.match(waves.csv(), /wave_point_equations_avg_us/);
// A preset with no post-warmup samples reports a failure, not a zero-FPS score.
// Earlier measurements remain available in the partial report.
const noSamples = new BenchmarkResults(entries);
noSamples.consume(start);
noSamples.consume({ event: "begin", index: 0 });
noSamples.consume(result);
noSamples.consume({ event: "begin", index: 1 });
assert.throws(
  () =>
    noSamples.consume(
      JSON.parse(
        '{"event":"error","index":1,"message":"No measured frames for this preset"}',
      ),
    ),
  /No measured frames for this preset/,
);
assert.equal(noSamples.state.status, "error");
assert.equal(noSamples.state.current, 1);
assert.equal(noSamples.state.results.length, 1);
assert.equal(noSamples.state.results[0].steady_fps, result.steady_fps);

const incomplete = new BenchmarkResults(entries);
incomplete.consume(start);
assert.throws(() => incomplete.consume({ event: "complete", total: 2 }));
assert.equal(incomplete.state.status, "incomplete");
const borderline = new BenchmarkResults(entries);
borderline.consume(start);
borderline.consume({ event: "begin", index: 0 });
borderline.consume({ ...result, work_p95_us: 21000 });
assert.equal(borderline.state.results[0].classification, "borderline");
assert.throws(() =>
  new BenchmarkResults(entries).consume({ ...start, total: 3 }),
);
for (const overrides of [
  { budget_us: 0 },
  { budget_us: undefined },
  { duration_us: NaN },
  { warmup_us: start.duration_us },
]) {
  assert.throws(() =>
    new BenchmarkResults(entries).consume({ ...start, ...overrides }),
  );
}
const unordered = new BenchmarkResults(entries);
unordered.consume(start);
assert.throws(() => unordered.consume(result));
assert.throws(() => unordered.consume({ event: "begin", index: 1 }));
unordered.consume({ event: "begin", index: 0 });
assert.throws(() => unordered.consume({ event: "begin", index: 0 }));
for (const overrides of [
  { frames: 500.5 },
  { warmup_frames: 0 },
  { render_avg_us: NaN },
]) {
  assert.throws(() => unordered.consume({ ...result, ...overrides }));
}
console.log(
  "PASS: timing validation, identities, classifications, CSV escaping and completion integrity",
);

// Exercise real launch/stream/save/exit handling against a local mock PS2Link client.
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "stroom-benchmark-"));
try {
  const build = path.join(directory, "build");
  fs.mkdirSync(path.join(build, "generated"), { recursive: true });
  const root = path.join(directory, "source");
  fs.mkdirSync(root);
  const launchEntries = entries.map((entry) => {
    if (entry.status !== "INCLUDED") return entry;
    fs.writeFileSync(path.join(root, entry.file), "per_frame_1=wave_r=1;");
    return {
      ...entry,
      sha256: createHash("sha256")
        .update(fs.readFileSync(path.join(root, entry.file)))
        .digest("hex"),
    };
  });
  const largeEntries = [
    ...launchEntries,
    ...Array.from({ length: 10 }, (_, index) => {
      const file = `extra-${index}.milk`;
      fs.writeFileSync(path.join(root, file), "per_frame_1=wave_r=1;");
      return {
        status: "INCLUDED",
        file,
        sha256: createHash("sha256")
          .update(fs.readFileSync(path.join(root, file)))
          .digest("hex"),
      };
    }),
  ];
  const client = path.join(directory, "client");
  let serial = 0;
  for (const { complete, full, count = 2 } of [
    { complete: true, full: true },
    { complete: false, full: true },
    { complete: true, full: false },
    { complete: true, full: true, count: 12 },
    { complete: false, full: true, count: 12 },
    { complete: true, full: false, count: 12 },
  ]) {
    const runEntries = count === 12 ? largeEntries : launchEntries;
    const archiveDirectory = path.join(directory, "archive-" + serial++);
    fs.writeFileSync(
      path.join(build, "generated/preset-report.json"),
      JSON.stringify({
        entries: runEntries,
        selection: full ? "all-compatible" : "quick",
        preset_root: root,
        benchmark_dir: archiveDirectory,
      }),
    );
    const events = [
      {
        ...start,
        total: count,
        video_mode: "480p",
        refresh_hz: 59.94006,
        render_profile: full ? 0 : 1,
        detail_profile: 0,
        wave_profile: 0,
      },
    ];
    for (let index = 0; index < (complete ? count : 1); ++index)
      events.push(
        { event: "begin", index },
        {
          ...profile,
          index,
          steady_fps: index === 1 ? 20 : 50,
          work_avg_us: profile.work_avg_us + index * 100,
          render_avg_us: profile.render_avg_us + index * 100,
        },
      );
    events.push({ event: "complete", total: count });
    const text = events.map((event) => JSON.stringify(event) + "\n").join("");
    const middle = Math.floor(text.length / 2);
    fs.writeFileSync(
      client,
      `#!/usr/bin/env node
const fs = require("fs");
process.stdout.write("loader output\\n");
fs.appendFileSync("benchmark-events.jsonl", ${JSON.stringify(text.slice(0, middle))});
setTimeout(() => fs.appendFileSync("benchmark-events.jsonl", ${JSON.stringify(text.slice(middle))}), 250);
setInterval(() => {}, 1000);
`,
    );
    fs.chmodSync(client, 0o700);
    const execution = spawnSync(
      process.execPath,
      [
        new URL("../../../tools/milkdrop/benchmark.mjs", import.meta.url)
          .pathname,
        "127.0.0.1",
        build,
      ],
      {
        env: { ...process.env, PS2CLIENT: client },
        encoding: "utf8",
        timeout: 5000,
      },
    );
    assert.equal(execution.status, complete ? 0 : 1, execution.stderr);
    const reports = path.join(build, "benchmark");
    assert.ok(!fs.existsSync(path.join(reports, "latest.txt")));
    const latest = path.join(
      reports,
      fs
        .readdirSync(reports, { withFileTypes: true })
        .filter((entry) => entry.isDirectory())
        .map((entry) => entry.name)
        .sort()
        .at(-1),
    );
    const saved = JSON.parse(
      fs.readFileSync(path.join(latest, "results.json"), "utf8"),
    );
    assert.equal(saved.status, complete ? "complete" : "incomplete");
    assert.equal(saved.results.length, complete ? count : 1);
    if (complete) {
      assert.equal(saved.current, null);
      assert.ok(!Object.hasOwn(saved, "error"));
      assert.equal(saved.results.length, saved.configuration.total);
    }
    assert.match(
      fs.readFileSync(path.join(latest, "console.log"), "utf8"),
      /loader output/,
    );
    assert.ok(fs.existsSync(path.join(latest, "results.csv")));
    if (full && complete) {
      const archives = fs.readdirSync(archiveDirectory);
      assert.equal(archives.length, 1);
      assert.deepEqual(
        JSON.parse(fs.readFileSync(path.join(archiveDirectory, archives[0]))),
        saved,
      );
      assert.equal(
        saved.results.filter((result) => result.quick === true).length,
        count >= 9 ? 9 : 0,
      );
      assert.ok(
        saved.results.every((result) =>
          count >= 9
            ? typeof result.quick === "boolean"
            : result.quick === null,
        ),
      );
      assert.match(execution.stdout, /Benchmark archived:/);
    } else {
      assert.equal(fs.existsSync(archiveDirectory), false);
      assert.ok(saved.results.every((result) => result.quick === null));
    }
  }
} finally {
  fs.rmSync(directory, { recursive: true, force: true });
}
console.log(
  "PASS: chunked host-file capture, complete/partial report persistence and client shutdown",
);
