import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import {
  mkdtempSync,
  mkdirSync,
  writeFileSync,
  existsSync,
  rmSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const root = process.argv[2];
const temp = mkdtempSync(join(tmpdir(), "stroom-clean-"));

try {
  for (const name of ["default", "command-line", "environment"]) {
    const cwd = join(temp, name);
    const sdk = join(cwd, "build/sdk");
    const installer = join(cwd, "build/installer");
    const external = join(cwd, "external");
    for (const directory of [
      join(sdk, "release"),
      join(sdk, "diagnostic"),
      installer,
      join(cwd, "build/stroom/480i/app"),
      join(cwd, "build/stroom/480p/app"),
      join(cwd, "build/stroom-diagnostics/480i/ps2link/memcard"),
      join(cwd, "build/stroom/480i/tests"),
      join(cwd, "build/deps-other"),
      external,
    ]) {
      mkdirSync(directory, { recursive: true });
      writeFileSync(join(directory, "sentinel"), name);
    }
    const overrides = {
      BUILD_DIR_BASE: external,
      BUILD_DIR_SDK: join(cwd, "build"),
      BUILD_DIR_INSTALLER: external,
      STROOM_SDK_ROOT: external,
    };
    const run = (target) =>
      execFileSync(
        "make",
        [
          "-f",
          resolve(root, "Makefile"),
          target,
          ...(name === "command-line"
            ? Object.entries(overrides).map(([key, value]) => `${key}=${value}`)
            : name === "environment"
              ? ["-e"]
              : []),
        ],
        {
          cwd,
          env: { ...process.env, ...(name === "environment" ? overrides : {}) },
          stdio: "pipe",
        },
      );

    run("clean");
    for (const variant of ["release", "diagnostic"])
      assert(
        existsSync(join(sdk, variant, "sentinel")),
        `${name}: ${variant} SDK deleted`,
      );
    assert(
      existsSync(join(installer, "sentinel")),
      `${name}: installer deleted`,
    );
    for (const directory of ["stroom", "stroom-diagnostics", "deps-other"])
      assert(
        !existsSync(join(cwd, "build", directory)),
        `${name}: ${directory} not cleaned`,
      );
    assert(
      existsSync(join(external, "sentinel")),
      `${name}: external files deleted`,
    );

    run("clean-all");
    assert(!existsSync(sdk), `${name}: SDK survived clean-all`);
    assert(!existsSync(installer), `${name}: installer survived clean-all`);
    assert(
      existsSync(join(external, "sentinel")),
      `${name}: external files deleted`,
    );
    run("clean");
  }
  console.log(
    "PASS: fixed build roots ignore overrides; clean preserves SDKs and installer; clean-all removes both",
  );
} finally {
  rmSync(temp, { recursive: true, force: true });
}
