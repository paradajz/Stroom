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
    "custom",
    "missing-host",
    "invalid-channel",
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
      assert.ok(mode !== "missing-host" && mode !== "invalid-channel");
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
    if (mode !== "missing-host") process.argv.push("ps2.local");
    if (mode === "custom")
      process.argv.push(
        "Desktop",
        "--input",
        'USB "Audio" & Mix',
        "--left",
        "9",
        "--right",
        "10",
      );
    if (mode === "invalid-channel") process.argv.push("--left", "0");
    process.exitCode = 0;
    await import(
      new URL(
        `../../../../../tools/aria/macos-service.mjs?${mode}`,
        import.meta.url,
      )
    );
    const success = mode === "success" || mode === "custom";
    assert.equal(process.exitCode, success ? 0 : 1);
    if (success) {
      const contents = readFileSync(plist, "utf8");
      assert.ok(contents.includes("macos-listen.mjs"));
      assert.ok(contents.includes("<string>ps2.local</string>"));
      if (mode === "custom") {
        assert.ok(contents.includes("<string>Desktop</string>"));
        assert.ok(
          contents.includes("<string>USB &quot;Audio&quot; &amp; Mix</string>"),
        );
        assert.match(
          contents,
          /<string>--left<\/string>\s*<string>9<\/string>/u,
        );
        assert.match(
          contents,
          /<string>--right<\/string>\s*<string>10<\/string>/u,
        );
      } else {
        assert.ok(contents.includes("<string>Audio input</string>"));
        assert.match(
          contents,
          /<string>--input<\/string>\s*<string>default<\/string>/u,
        );
        assert.ok(!contents.includes("<string>--left</string>"));
        assert.ok(!contents.includes("<string>--right</string>"));
      }
      assert.equal(loaded, true);
    } else {
      assert.equal(existsSync(plist), hadPrevious);
      if (hadPrevious)
        assert.equal(readFileSync(plist, "utf8"), "original configuration");
      assert.equal(loaded, hadPrevious && mode !== "rollback");
    }
    if (mode === "validation") assert.deepEqual(actions, ["validate"]);
    if (mode === "missing-host" || mode === "invalid-channel")
      assert.deepEqual(actions, []);
    if (mode !== "rollback")
      assert.ok(
        !readdirSync(agents).some((name) =>
          name.startsWith(".stroom-install-"),
        ),
      );
  }
  console.log(
    "PASS: capture defaults and overrides, invalid configuration leaves service untouched, validation before stopping, startup rollback and cleanup.",
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
