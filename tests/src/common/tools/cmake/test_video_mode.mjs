import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import fs from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const [root, cmake] = process.argv.slice(2);
const directory = fs.mkdtempSync(path.join(tmpdir(), "stroom-video-mode-"));
const run = (command, args, options = {}) =>
  execFileSync(command, args, { encoding: "utf8", stdio: "pipe", ...options });
try {
  fs.writeFileSync(
    path.join(directory, "CMakeLists.txt"),
    `
cmake_minimum_required(VERSION 3.21)
project(video_mode C)
include("${root}/cmake/project-options.cmake")
add_executable(probe probe.c)
target_include_directories(probe PRIVATE "${root}/src/common"
  "${root}/tests/src/common/platform/graphics/stubs")
add_subdirectory("${root}/third_party/Unity" unity EXCLUDE_FROM_ALL)
add_executable(display-test "${root}/tests/src/common/platform/graphics/test_display.c"
  "${root}/src/common/platform/graphics/display.c")
target_link_libraries(display-test PRIVATE unity::framework)
target_include_directories(display-test PRIVATE "${root}/src/common"
  "${root}/tests/src/common/platform/graphics/stubs")
`,
  );
  fs.writeFileSync(
    path.join(directory, "probe.c"),
    `
#include <gsKit.h>
#include "platform/graphics/display_config.h"
#include "contracts/milkdrop.h"
#include <stdio.h>
int main(void) {
  printf("%s %d %d %d %d %d %u %u %.2f %.2f", DISPLAY_MODE_NAME,
    DISPLAY_GS_MODE == GS_MODE_NTSC, DISPLAY_INTERLACE == GS_INTERLACED,
    DISPLAY_FIELD_MODE == (DISPLAY_INTERLACED ? GS_FIELD : GS_FRAME), DISPLAY_WIDTH, DISPLAY_HEIGHT,
    (unsigned)DISPLAY_REFRESH_NUMERATOR, (unsigned)DISPLAY_REFRESH_DENOMINATOR,
    (double)MILKDROP_FPS_BASELINE_MINIMUM, (double)MILKDROP_FPS_HIGH_MINIMUM);
  return 0;
}
`,
  );
  const build = path.join(directory, "build");
  const configure = (mode) =>
    run(cmake, [
      "-S",
      directory,
      "-B",
      build,
      ...(mode ? [`-DSTROOM_VIDEO_MODE=${mode}`] : []),
    ]);
  // Switch repeatedly in the same build: old generated headers must not survive.
  for (const mode of [undefined, "480i", "480p", "480i"]) {
    configure(mode);
    run(cmake, ["--build", build]);
    const interlaced = (mode ?? "480i") === "480i";
    assert.equal(
      run(path.join(build, "probe"), []),
      `${mode ?? "480i"} ${interlaced ? "1 1" : "0 0"} 1 640 480 60000 1001 29.97 59.94`,
    );
    run(path.join(build, "display-test"), []);

    // PS2Link invokes this same generator outside CMake; its selected header must agree.
    const generated = path.join(directory, "launcher/contracts");
    const environment = { ...process.env };
    delete environment.STROOM_VIDEO_MODE;
    if (mode) environment.STROOM_VIDEO_MODE = mode;
    run(
      process.execPath,
      [path.join(root, "tools/contracts/generate.mjs"), generated],
      { env: environment },
    );
    assert.equal(
      fs.readFileSync(path.join(generated, "display.h"), "utf8"),
      fs.readFileSync(
        path.join(build, "generated/contracts/display.h"),
        "utf8",
      ),
    );
  }
  assert.throws(() => configure("bad-mode"), /Unknown STROOM_VIDEO_MODE/);
  const invalid = spawnSync(
    process.execPath,
    [
      path.join(root, "tools/contracts/generate.mjs"),
      path.join(directory, "invalid"),
    ],
    {
      encoding: "utf8",
      env: { ...process.env, STROOM_VIDEO_MODE: "bad-mode" },
    },
  );
  assert.notEqual(invalid.status, 0);
  assert.match(invalid.stderr, /Unknown STROOM_VIDEO_MODE/);
  const invalidLauncher = spawnSync(
    "sh",
    [path.join(root, "tools/ps2link/build.sh"), "--video-mode", "bad-mode"],
    { encoding: "utf8" },
  );
  assert.notEqual(invalidLauncher.status, 0);
  assert.match(invalidLauncher.stderr, /Unknown video mode/);

  for (const preset of ["stroom", "stroom-diagnostics"]) {
    for (const mode of ["480i", "480p"]) {
      for (const target of [
        "prod",
        "dev",
        "run",
        "benchmark",
        "lint-release",
        "configure-tests",
        "diagnostic",
      ]) {
        const selectedPreset =
          target === "benchmark"
            ? "benchmark-full"
            : target === "lint-release"
              ? "stroom"
              : preset;
        const output = path.join(root, "build", selectedPreset, mode);
        const command = run(
          "make",
          ["--dry-run", target, `PRESET=${preset}`, `VIDEO_MODE=${mode}`],
          { cwd: root },
        );
        assert(
          command.includes(
            target === "dev"
              ? `--video-mode "${mode}"`
              : target === "diagnostic"
                ? "node tools/diagnostic/capture.mjs"
                : `-DSTROOM_VIDEO_MODE="${mode}"`,
          ),
        );
        const expected =
          target === "dev"
            ? "ps2link/PS2LINK.ELF"
            : target === "configure-tests"
              ? "tests"
              : target === "diagnostic"
                ? "diagnostics"
                : target === "lint-release"
                  ? "lint/app"
                  : "app";
        assert(
          command.includes(path.join(output, expected)),
          `${target}: missing mode-specific output path`,
        );
        assert(
          !command.includes(
            path.join(
              root,
              "build",
              selectedPreset,
              mode === "480i" ? "480p" : "480i",
            ),
          ),
        );
        if (["prod", "dev"].includes(target)) {
          assert(
            command.includes(
              `node tools/setup/prepare.mjs ${target} "${path.join(output, target === "prod" ? "app/stroom-packed.elf" : "ps2link/PS2LINK.ELF")}"`,
            ),
          );
        }
        if (["prod", "dev", "run"].includes(target)) {
          const variant = preset === "stroom" ? "release" : "diagnostic";
          assert(
            target === "dev"
              ? command.includes(
                  `--sdk-dir "${path.join(root, "build/sdk")}" --variant "${variant}"`,
                )
              : command.includes(path.join(root, "build/sdk", variant)),
            `${preset}/${mode}/${target}: wrong SDK`,
          );
        }
      }
    }
  }
  const defaults = { ...process.env };
  for (const key of ["PRESET", "VIDEO_MODE", "MAKEFLAGS", "MAKEOVERRIDES"])
    delete defaults[key];
  for (const target of ["prod", "dev", "run"]) {
    const command = run("make", ["--dry-run", target], {
      cwd: root,
      env: defaults,
    });
    assert(command.includes(path.join(root, "build/stroom/480i")));
    assert(!command.includes(path.join(root, "build/stroom/480p")));
    assert(
      command.includes(
        target === "dev" ? '--video-mode "480i"' : '-DSTROOM_VIDEO_MODE="480i"',
      ),
    );
  }
  assert.throws(
    () =>
      run("make", ["--dry-run", "prod", "VIDEO_MODE=../sdk"], { cwd: root }),
    /Unknown VIDEO_MODE/,
  );
  console.log(
    "PASS: display modes agree; outputs are isolated by preset then video mode; release and diagnostic builds select their matching SDK",
  );
} finally {
  fs.rmSync(directory, { recursive: true, force: true });
}
