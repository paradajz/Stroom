import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const [root, compiler] = process.argv.slice(2);
const fixture = path.join(root, "tests/src/common/tools/ps2link");
const directory = fs.mkdtempSync(
  path.join(tmpdir(), "stroom-ps2link-startup-"),
);
const run = (command, args, options = {}) => {
  try {
    return execFileSync(command, args, {
      encoding: "utf8",
      stdio: "pipe",
      ...options,
    });
  } catch (error) {
    throw new Error(
      `${command} failed:\n${error.stdout ?? ""}${error.stderr ?? ""}`,
      { cause: error },
    );
  }
};
try {
  fs.mkdirSync(path.join(directory, "ee"));
  for (const file of ["ps2link.c", "cmdHandler.c"]) {
    fs.copyFileSync(
      path.join(root, "third_party/ps2link/ee", file),
      path.join(directory, "ee", file),
    );
  }
  // Apply the actual source changes. The full PS2 build also applies the Makefile hunk.
  const patch = fs
    .readFileSync(path.join(root, "patches/ps2link/direct-boot.patch"), "utf8")
    .split("--- a/ee/Makefile")[0];
  run("patch", ["--batch", "--fuzz=0", "-p1"], {
    cwd: directory,
    input: patch,
  });
  const includes = [
    path.join(fixture, "startup_stubs"),
    path.join(root, "third_party/ps2link/include"),
    path.join(root, "src/common"),
    path.join(root, "third_party/Unity/src"),
  ].flatMap((name) => ["-I", name]);
  const object = path.join(directory, "ps2link.o");
  run(compiler, [
    "-std=gnu11",
    "-Dmain=ps2link_main",
    '-DAPP_VERSION="test"',
    "-Wno-pointer-to-int-cast",
    ...includes,
    "-c",
    path.join(directory, "ee/ps2link.c"),
    "-o",
    object,
  ]);
  const executable = path.join(directory, "startup");
  run(compiler, [
    "-std=gnu11",
    ...includes,
    path.join(fixture, "test_startup.c"),
    path.join(root, "third_party/Unity/src/unity.c"),
    object,
    "-o",
    executable,
  ]);

  const config = "192.168.9.22 255.255.255.0 192.168.9.1\n";
  // A conflicting working-directory config must not override the boot file's settings.
  fs.writeFileSync(
    path.join(directory, "IPCONFIG.DAT"),
    "10.0.0.99 255.0.0.0 10.0.0.1\n",
  );
  for (const destination of [
    "mc0:/BOOT/IPCONFIG.DAT",
    "mc1:/apps/IPCONFIG.DAT",
    "host:IPCONFIG.DAT",
  ]) {
    const filename = path.join(directory, destination);
    fs.mkdirSync(path.dirname(filename), { recursive: true });
    fs.writeFileSync(filename, config);
  }
  for (const scenario of [
    "mc0:/BOOT/BOOT.ELF",
    "mc1:/apps/PS2LINK.ELF",
    "host:PS2LINK.ELF",
    "missing",
    "empty",
    "null",
  ]) {
    const environment = {
      ...process.env,
      LAUNCH_PATH: scenario,
      EXPECTED_IP: "192.168.9.22",
      EXPECTED_NETMASK: "255.255.255.0",
      EXPECTED_GATEWAY: "192.168.9.1",
    };
    delete environment.CARD_FAIL;
    run(executable, [], { cwd: directory, env: environment });
  }
  run(executable, [], {
    cwd: directory,
    env: {
      ...process.env,
      LAUNCH_PATH: "mc0:/BOOT/BOOT.ELF",
      CARD_FAIL: "1",
      EXPECTED_IP: "192.168.1.10",
      EXPECTED_NETMASK: "255.255.255.0",
      EXPECTED_GATEWAY: "192.168.1.0",
    },
  });
  console.log(
    "PS2Link direct startup: explicit/missing paths, local settings, service failures and reset ordering passed",
  );
} finally {
  fs.rmSync(directory, { recursive: true, force: true });
}
