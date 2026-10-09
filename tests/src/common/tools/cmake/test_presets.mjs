import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const [root, cmake] = process.argv.slice(2);
const directory = mkdtempSync(join(tmpdir(), "stroom-preset-selection-"));
try {
  // Exercise real preset inheritance and project options without requiring a PS2 SDK.
  const presets = JSON.parse(
    readFileSync(join(root, "src/CMakePresets.json"), "utf8"),
  );
  for (const preset of presets.configurePresets) delete preset.toolchainFile;
  writeFileSync(
    join(directory, "CMakePresets.json"),
    JSON.stringify(presets).replaceAll("${sourceDir}", join(root, "src")),
  );
  writeFileSync(
    join(directory, "CMakeLists.txt"),
    [
      "cmake_minimum_required(VERSION 3.21)",
      "project(preset_selection NONE)",
      `include("${root}/cmake/project-options.cmake")`,
      "get_property(cached_directory CACHE MILKDROP_BENCHMARK_DIR PROPERTY VALUE)",
      'file(WRITE "${CMAKE_BINARY_DIR}/selection.txt" "${MILKDROP_BENCHMARK_DIR}\\n${cached_directory}\\n${MILKDROP_PRESET_SELECTION}\\n${STROOM_PRESET_BENCHMARK}\\n${STROOM_APP}")',
    ].join("\n"),
  );
  const configure = (preset, args = []) => {
    execFileSync(
      cmake,
      [
        "-S",
        directory,
        "-B",
        join(directory, "build"),
        "--preset",
        preset,
        ...args,
      ],
      { stdio: "pipe" },
    );
    const [effective, cached, selection, benchmark, app] = readFileSync(
      join(directory, "build/selection.txt"),
      "utf8",
    ).split("\n");
    return { effective, cached, selection, benchmark, app };
  };
  const migrated = configure("stroom", [
    "-DMILKDROP_BENCHMARK_DIR=" + join(root, "milkdrop/benchmark"),
  ]);
  assert.equal(
    migrated.effective,
    join(root, "src/common/milkdrop/benchmarks"),
  );
  assert.equal(migrated.cached, migrated.effective);
  const previous = configure("stroom", [
    "-DMILKDROP_BENCHMARK_DIR=" + join(root, "src/milkdrop/benchmarks"),
  ]);
  assert.equal(previous.effective, migrated.effective);
  assert.equal(previous.cached, migrated.effective);
  for (const playback of [
    join(root, "src/common/milkdrop/benchmarks"),
    join(directory, "custom"),
  ]) {
    configure("stroom", ["-DMILKDROP_BENCHMARK_DIR=" + playback]);
    for (const normal of ["stroom-diagnostics", "stroom"]) {
      const quick = configure("benchmark-quick");
      assert.equal(quick.cached, playback);
      assert.equal(resolve(quick.effective), playback);
      assert.equal(quick.selection, "quick");
      assert.equal(quick.app, "benchmark");
      assert.equal(quick.benchmark, "ON");
      const restored = configure(normal);
      assert.equal(restored.cached, playback);
      assert.equal(restored.effective, playback);
      assert.equal(restored.selection, "playback");
      assert.equal(restored.benchmark, "OFF");
      assert.equal(restored.app, "stroom");
    }
    const full = configure("benchmark-full");
    assert.equal(full.cached, playback);
    assert.equal(full.selection, "all-compatible");
    assert.equal(full.app, "benchmark");
    assert.equal(configure("stroom").effective, playback);
  }
} finally {
  rmSync(directory, { recursive: true, force: true });
}
console.log("PASS: benchmark preset switches preserve the benchmark directory");
