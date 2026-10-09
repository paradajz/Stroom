import assert from "node:assert/strict";
import {
  cpSync,
  existsSync,
  mkdtempSync,
  mkdirSync,
  readFileSync,
  writeFileSync,
  statSync,
  rmSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { execFileSync, spawnSync } from "node:child_process";

const root = fileURLToPath(new URL("../../../../../", import.meta.url));
const temp = mkdtempSync(join(tmpdir(), "stroom-contracts-"));
const run = (program, args) =>
  execFileSync(program, args, {
    encoding: "utf8",
    stdio: ["ignore", "pipe", "pipe"],
  });
try {
  for (const dir of ["shared", "tools/contracts"])
    cpSync(join(root, dir), join(temp, dir), { recursive: true });
  mkdirSync(join(temp, "cmake"));
  cpSync(
    join(root, "cmake/contracts.cmake"),
    join(temp, "cmake/contracts.cmake"),
  );
  writeFileSync(
    join(temp, "CMakeLists.txt"),
    `
cmake_minimum_required(VERSION 3.21)
project(contract_regeneration C)
set(STROOM_ROOT "${temp}")
set(GENERATED_DIR "${temp}/build/generated")
set(NODE_EXECUTABLE "${process.execPath}")
include(cmake/contracts.cmake)
add_executable(probe probe.c)
`,
  );
  writeFileSync(
    join(temp, "probe.c"),
    '#include "contracts/metadata.h"\n#include <stdio.h>\nint main(void) { printf("%u", METADATA_TEXT_BYTES); return 0; }\n',
  );
  const build = join(temp, "build");
  run("cmake", ["-S", temp, "-B", build]);
  run("cmake", ["--build", build]);
  const definition = join(temp, "shared/contracts/metadata.json");
  const values = JSON.parse(readFileSync(definition));
  assert.equal(
    run(join(build, "probe"), []),
    String(values.constants.METADATA_TEXT_BYTES.value),
  );
  const diagnosticHeader = join(build, "generated/contracts/diagnostic.h");
  const diagnosticTime = statSync(diagnosticHeader).mtimeMs;
  values.constants.METADATA_TEXT_BYTES.value += 16;
  writeFileSync(definition, JSON.stringify(values));
  // No explicit reconfigure: CMake must notice the authored contract changed.
  run("cmake", ["--build", build]);
  assert.equal(
    run(join(build, "probe"), []),
    String(values.constants.METADATA_TEXT_BYTES.value),
  );
  assert.equal(
    statSync(diagnosticHeader).mtimeMs,
    diagnosticTime,
    "Unchanged headers must not cause needless rebuilds",
  );

  // Adding a definition needs no loader, generator or CMake membership edit.
  const extraFile = join(temp, "shared/contracts/extra.json");
  const extraHeader = join(build, "generated/contracts/extra.h");
  const extra = {
    EXTRA_VALUE: { value: 7, description: "Dynamic membership fixture." },
  };
  writeFileSync(extraFile, JSON.stringify(extra));
  run("cmake", ["--build", build]);
  assert.ok(
    readFileSync(extraHeader, "utf8").includes("#define EXTRA_VALUE 7"),
  );
  const registry = () =>
    JSON.parse(
      run(process.execPath, [
        "--input-type=module",
        "-e",
        "import { contracts } from " +
          JSON.stringify(
            pathToFileURL(join(temp, "tools/contracts/load.mjs")).href,
          ) +
          "; console.log(JSON.stringify(contracts));",
      ]),
    );
  assert.deepEqual(registry().extra, { EXTRA_VALUE: 7 });
  const probe = join(temp, "probe.c");
  const originalProbe = readFileSync(probe, "utf8");
  // Refresh edits must change the shared cutoffs in both scripts and built C.
  writeFileSync(
    probe,
    '#include "contracts/milkdrop.h"\n#include <stdio.h>\nint main(void) { printf("%.2f %.2f", (double)MILKDROP_FPS_BASELINE_MINIMUM, (double)MILKDROP_FPS_HIGH_MINIMUM); return 0; }\n',
  );
  run("cmake", ["--build", build]);
  assert.equal(run(join(build, "probe"), []), "29.97 59.94");
  const displayFile = join(temp, "shared/contracts/display.json");
  const display = JSON.parse(readFileSync(displayFile));
  display.DISPLAY_REFRESH_NUMERATOR.value = 50000;
  writeFileSync(displayFile, JSON.stringify(display));
  run("cmake", ["--build", build]);
  assert.equal(run(join(build, "probe"), []), "24.98 49.95");
  assert.equal(registry().milkdrop.MILKDROP_FPS_BASELINE_MINIMUM, 24.98);
  assert.equal(registry().milkdrop.MILKDROP_FPS_HIGH_MINIMUM, 49.95);
  writeFileSync(
    probe,
    '#include "contracts/extra.h"\n#include <stdio.h>\nint main(void) { printf("%u", EXTRA_VALUE); return 0; }\n',
  );
  run("cmake", ["--build", build]);
  assert.equal(run(join(build, "probe"), []), "7");
  extra.EXTRA_VALUE.value = 11;
  writeFileSync(extraFile, JSON.stringify(extra));
  run("cmake", ["--build", build]);
  assert.equal(run(join(build, "probe"), []), "11");
  assert.equal(registry().extra.EXTRA_VALUE, 11);
  // Removal also regenerates, without deleting headers the generator does not own.
  const manualHeader = join(build, "generated/contracts/manual.h");
  writeFileSync(manualHeader, "/* Not generated. */\n");
  rmSync(extraFile);
  writeFileSync(probe, originalProbe);
  run("cmake", ["--build", build]);
  assert.equal(existsSync(extraHeader), false);
  assert.equal(Object.hasOwn(registry(), "extra"), false);
  assert.equal(readFileSync(manualHeader, "utf8"), "/* Not generated. */\n");

  values.constants.WRONG_PREFIX = {
    value: 1,
    description: "Invalid prefix fixture.",
  };
  writeFileSync(definition, JSON.stringify(values));
  const invalidPrefix = spawnSync(
    process.execPath,
    [join(temp, "tools/contracts/generate.mjs"), join(temp, "invalid")],
    { encoding: "utf8" },
  );
  assert.notEqual(invalidPrefix.status, 0);
  assert.match(invalidPrefix.stderr, /Invalid contract: WRONG_PREFIX/);
  delete values.constants.WRONG_PREFIX;
  writeFileSync(definition, JSON.stringify(values));

  // Wire IDs must be unique unsigned bytes; JSON order must not assign IDs.
  const originalId = values.actions[1].id;
  for (const invalidId of [values.actions[0].id, -1, 256, 1.5]) {
    values.actions[1].id = invalidId;
    writeFileSync(definition, JSON.stringify(values));
    const result = spawnSync(
      process.execPath,
      [join(temp, "tools/contracts/generate.mjs"), join(temp, "invalid")],
      { encoding: "utf8" },
    );
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /Duplicate or invalid metadata action/);
  }
  values.actions[1].id = originalId;
  values.actions.reverse();
  writeFileSync(definition, JSON.stringify(values));
  run(process.execPath, [
    join(temp, "tools/contracts/generate.mjs"),
    join(temp, "reordered"),
  ]);
  const reordered = readFileSync(join(temp, "reordered/metadata.h"), "utf8");
  for (const action of values.actions)
    assert(reordered.includes(`#define ${action.constant} ${action.id}\n`));

  const benchmarkFile = join(temp, "shared/contracts/benchmark.json");
  const benchmark = JSON.parse(readFileSync(benchmarkFile));
  const originalFields = structuredClone(benchmark.profileFields);
  for (const invalid of [
    { name: originalFields[0].name },
    { name: "bad-name" },
    { group: "unknown" },
    { description: "" },
  ]) {
    benchmark.profileFields = structuredClone(originalFields);
    Object.assign(benchmark.profileFields[1], invalid);
    writeFileSync(benchmarkFile, JSON.stringify(benchmark));
    const result = spawnSync(
      process.execPath,
      [join(temp, "tools/contracts/generate.mjs"), join(temp, "invalid")],
      { encoding: "utf8" },
    );
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /Duplicate or invalid benchmark profile field/);
  }
  benchmark.profileFields = originalFields;
  benchmark.profileFields.push({
    name: "probe_field",
    group: "wave",
    description: "Regeneration fixture.",
  });
  writeFileSync(benchmarkFile, JSON.stringify(benchmark));
  run("cmake", ["--build", build]);
  assert.match(
    readFileSync(join(build, "generated/contracts/benchmark.h"), "utf8"),
    /X\(probe_field\)/,
  );

  const diagnosticFile = join(temp, "shared/contracts/diagnostic.json");
  const diagnostic = JSON.parse(readFileSync(diagnosticFile));
  const originalPhases = structuredClone(diagnostic.artworkPhases);
  for (const invalid of [
    { id: originalPhases[0].id },
    { id: 0 },
    { id: 256 },
    { id: 1.5 },
    { name: originalPhases[0].name },
    { constant: originalPhases[0].constant },
    { constant: "ARTWORK_BAD_PREFIX" },
    { description: "" },
  ]) {
    diagnostic.artworkPhases = structuredClone(originalPhases);
    Object.assign(diagnostic.artworkPhases[1], invalid);
    writeFileSync(diagnosticFile, JSON.stringify(diagnostic));
    const result = spawnSync(
      process.execPath,
      [join(temp, "tools/contracts/generate.mjs"), join(temp, "invalid")],
      { encoding: "utf8" },
    );
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /Duplicate or invalid artwork phase/);
  }
  diagnostic.artworkPhases = originalPhases.toReversed();
  writeFileSync(diagnosticFile, JSON.stringify(diagnostic));
  run("cmake", ["--build", build]);
  const phaseHeader = readFileSync(diagnosticHeader, "utf8");
  for (const phase of originalPhases) {
    assert(phaseHeader.includes(`#define ${phase.constant} ${phase.id}\n`));
    assert.equal(registry().diagnostic[phase.constant], phase.id);
  }
  const phaseNames = JSON.parse(
    run(process.execPath, [
      "--input-type=module",
      "-e",
      "import { diagnosticArtworkPhaseNames } from " +
        JSON.stringify(
          pathToFileURL(join(temp, "tools/contracts/load.mjs")).href,
        ) +
        "; console.log(JSON.stringify(diagnosticArtworkPhaseNames));",
    ]),
  );
  assert.deepEqual(
    phaseNames,
    Object.fromEntries(originalPhases.map((p) => [p.id, p.name])),
  );
  diagnostic.events[1].id = diagnostic.events[0].id;
  writeFileSync(diagnosticFile, JSON.stringify(diagnostic));
  assert.notEqual(
    spawnSync(process.execPath, [
      join(temp, "tools/contracts/generate.mjs"),
      join(temp, "invalid"),
    ]).status,
    0,
  );
  console.log(
    "PASS: contract additions, edits and removals regenerate and rebuild C; unchanged headers stay untouched; duplicate event IDs rejected",
  );
} finally {
  rmSync(temp, { recursive: true, force: true });
}
