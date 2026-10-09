import assert from "node:assert/strict";
import {
  selectQuickPresets,
  markQuickPresets,
} from "../../../tools/milkdrop/select_quick_presets.mjs";

const file = (index) => `preset-${String(index).padStart(2, "0")}.milk`;
const run = (count) => ({
  status: "complete",
  current: null,
  configuration: {
    total: count,
    render_profile: 0,
    detail_profile: 0,
    wave_profile: 0,
  },
  results: Array.from({ length: count }, (_, index) => ({
    file: file(index),
    work_avg_us: (index + 1) * 1000,
    // All scores hit the same FPS ceiling; work time must still distinguish them.
    steady_fps: 59.94,
  })),
});
for (const [count, expected] of [
  [9, [8, 7, 6, 5, 4, 3, 2, 1, 0]],
  [11, [10, 9, 8, 6, 5, 4, 2, 1, 0]],
  [12, [11, 10, 9, 6, 5, 4, 2, 1, 0]],
]) {
  const report = run(count);
  const original = structuredClone(report);
  assert.deepEqual(selectQuickPresets(report), expected.map(file));
  assert.deepEqual(
    report,
    original,
    "Selection must not reorder the source report",
  );
}
const ties = run(12);
ties.results.reverse();
for (const result of ties.results) result.work_avg_us = 1000;
assert.deepEqual(
  selectQuickPresets(ties),
  [0, 1, 2, 5, 6, 7, 9, 10, 11].map(file),
);

for (const change of [
  (report) => {
    report.status = "incomplete";
  },
  (report) => {
    report.current = 0;
  },
  (report) => {
    report.error = "Failed measurement";
  },
  (report) => {
    report.configuration.total--;
  },
  (report) => {
    report.configuration.render_profile = 1;
  },
  (report) => {
    report.configuration.detail_profile = 1;
  },
  (report) => {
    report.configuration.wave_profile = 1;
  },
  (report) => {
    report.results[0].file = report.results[1].file;
  },
  (report) => {
    report.results[0].file = "../outside.milk";
  },
  (report) => {
    report.results[0].work_avg_us = NaN;
  },
  (report) => {
    report.results[0].work_avg_us = -1;
  },
]) {
  const report = run(12);
  change(report);
  assert.throws(() => selectQuickPresets(report));
}
assert.throws(() => selectQuickPresets(run(8)), /at least nine/);

const annotated = run(12);
markQuickPresets(annotated);
assert.deepEqual(
  annotated.results
    .filter((result) => result.quick)
    .map((result) => result.file),
  [0, 1, 2, 4, 5, 6, 9, 10, 11].map(file),
);
assert.ok(
  annotated.results.every((result) => typeof result.quick === "boolean"),
);
for (const report of [run(8), { ...run(12), status: "interrupted" }]) {
  markQuickPresets(report);
  assert.ok(report.results.every((result) => result.quick === null));
}
const profiled = run(12);
profiled.configuration.detail_profile = 1;
markQuickPresets(profiled);
assert.ok(profiled.results.every((result) => result.quick === null));
console.log(
  "PASS: distinct slow/middle/fast groups, odd/even median selection, stable ties, report validation and embedded quick flags",
);
