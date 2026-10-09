import assert from "node:assert/strict";
import cp from "node:child_process";
import os from "node:os";
import { syncBuiltinESMExports } from "node:module";
import {
  mkdtempSync,
  mkdirSync,
  writeFileSync,
  readFileSync,
  existsSync,
  readdirSync,
  rmSync,
} from "node:fs";
import { join } from "node:path";

const original = {
  spawnSync: cp.spawnSync,
  homedir: os.homedir,
  argv: process.argv,
  getuid: process.getuid,
  platform: Object.getOwnPropertyDescriptor(process, "platform"),
};
const root = mkdtempSync(join(os.tmpdir(), "stroom-install-"));
try {
  Object.defineProperty(process, "platform", {
    value: "darwin",
    configurable: true,
  });
  process.getuid = () => 501;
  for (const mode of [
    "success",
    "validation",
    "startup",
    "fresh-startup",
    "rollback",
  ]) {
    const home = join(root, mode);
    const agents = join(home, "Library/LaunchAgents");
    const plist = join(agents, "local.stroom.listening.plist");
    mkdirSync(agents, { recursive: true });
    const hadPrevious = mode !== "fresh-startup";
    if (hadPrevious) writeFileSync(plist, "original configuration");
    let loaded = hadPrevious;
    let boots = 0;
    const actions = [];
    os.homedir = () => home;
    cp.spawnSync = (command, args) => {
      if (command === "/usr/bin/which")
        return { status: 0, stdout: "/opt/homebrew/bin/ffmpeg" };
      if (command === "/usr/bin/plutil") {
        actions.push("validate");
        assert.notEqual(args[1], plist);
        assert.ok(readFileSync(args[1], "utf8").includes("<plist"));
        if (hadPrevious)
          assert.equal(readFileSync(plist, "utf8"), "original configuration");
        assert.equal(loaded, hadPrevious);
        return {
          status: mode === "validation" ? 1 : 0,
          stderr: "invalid replacement",
        };
      }
      assert.equal(command, "/bin/launchctl");
      if (args[0] === "print") return { status: loaded ? 0 : 1 };
      actions.push(args[0]);
      if (args[0] === "bootout") {
        loaded = false;
        return { status: 0 };
      }
      assert.equal(args[0], "bootstrap");
      ++boots;
      if (
        mode === "rollback" ||
        ((mode === "startup" || mode === "fresh-startup") && boots === 1)
      )
        return { status: 1, stderr: "bootstrap failed" };
      loaded = true;
      return { status: 0 };
    };
    syncBuiltinESMExports();
    process.argv = [process.execPath, "macos-service.mjs", "install"];
    process.exitCode = 0;
    await import(
      new URL(
        `../../../../../tools/aria/macos-service.mjs?${mode}`,
        import.meta.url,
      )
    );
    assert.equal(process.exitCode, mode === "success" ? 0 : 1);
    if (mode === "success") {
      assert.ok(readFileSync(plist, "utf8").includes("macos-listen.mjs"));
      assert.equal(loaded, true);
    } else {
      assert.equal(existsSync(plist), hadPrevious);
      if (hadPrevious)
        assert.equal(readFileSync(plist, "utf8"), "original configuration");
      assert.equal(loaded, hadPrevious && mode !== "rollback");
    }
    if (mode === "validation") assert.deepEqual(actions, ["validate"]);
    if (mode !== "rollback")
      assert.ok(
        !readdirSync(agents).some((name) =>
          name.startsWith(".stroom-install-"),
        ),
      );
  }
  console.log(
    "PASS: validation before stopping, successful replacement, startup rollback, fresh-install cleanup and rollback failure.",
  );
} finally {
  cp.spawnSync = original.spawnSync;
  os.homedir = original.homedir;
  syncBuiltinESMExports();
  process.argv = original.argv;
  process.getuid = original.getuid;
  Object.defineProperty(process, "platform", original.platform);
  process.exitCode = 0;
  rmSync(root, { recursive: true, force: true });
}
