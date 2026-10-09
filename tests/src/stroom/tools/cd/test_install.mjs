import assert from "node:assert/strict";
import cp from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import { createServer } from "node:net";
import { syncBuiltinESMExports } from "node:module";
import { join } from "node:path";

const original = {
  spawn: cp.spawnSync,
  access: fs.accessSync,
  read: fs.readFileSync,
  warn: console.warn,
  home: os.homedir,
  argv: process.argv,
  uid: process.getuid,
  exitCode: process.exitCode,
  platform: Object.getOwnPropertyDescriptor(process, "platform"),
  env: Object.fromEntries(
    ["PATH", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"].map(
      (key) => [key, process.env[key]],
    ),
  ),
};
const root = fs.mkdtempSync(join(os.tmpdir(), "stroom-cd-install-test-"));
try {
  for (const mode of [
    "fresh",
    "reinstall",
    "custom-config",
    "limits",
    "validation",
    "startup",
    "fresh-startup",
    "rollback",
    "missing-contact",
    "invalid-contact",
    "missing-ffmpeg",
    "manager",
    "busy",
    "uninstall",
    "unsupported",
    "root",
    "unmanaged",
  ]) {
    const home = join(root, mode + ' space%$"');
    const application = join(home, ".local/share/stroom/cd-recognition");
    const units = join(home, ".config/systemd/user");
    const unit = join(units, "stroom-cd-recognition.service");
    const cache = join(home, ".cache/stroom/cd");
    const configuration = join(
      home,
      mode === "custom-config" ? "custom.json" : ".config/stroom/config.json",
    );
    const settings = {
      contact:
        mode === "missing-contact"
          ? ""
          : mode === "invalid-contact"
            ? "invalid\ncontact"
            : 'contact$USER%name"tag@example.test',
      cache_directory: cache,
      log_statistics: mode === "fresh",
    };
    fs.mkdirSync(join(configuration, ".."), { recursive: true });
    fs.writeFileSync(configuration, JSON.stringify(settings));
    const previousConfiguration = fs.readFileSync(configuration, "utf8");
    const hadPrevious = !["fresh", "fresh-startup", "busy"].includes(mode);
    fs.mkdirSync(cache, { recursive: true });
    fs.writeFileSync(join(cache, "retained.json"), "cached labels");
    if (hadPrevious) {
      fs.mkdirSync(application, { recursive: true });
      fs.mkdirSync(units, { recursive: true });
      fs.writeFileSync(
        join(application, "previous.txt"),
        "previous application",
      );
      fs.writeFileSync(unit, "previous unit");
      if (mode !== "unmanaged")
        fs.writeFileSync(
          join(application, "installation.json"),
          JSON.stringify({ service: "stroom-cd-recognition.service" }),
        );
    }
    let active = hadPrevious,
      enabled = hadPrevious,
      starts = 0;
    const actions = [];
    const warnings = [];
    console.warn = (message) => warnings.push(message);
    fs.readFileSync = (path, ...args) => {
      if (String(path).startsWith("/sys/fs/cgroup/fixture-cd/")) {
        const values = {
          "memory.max": "1073741824",
          "memory.high": "536870912",
          "memory.swap.max": "0",
          "pids.max": "128",
          "cpu.max": "100000 100000",
        };
        return values[String(path).split("/").at(-1)];
      }
      return original.read(path, ...args);
    };
    os.homedir = () => home;
    process.getuid = () => (mode === "root" ? 0 : 1000);
    Object.defineProperty(process, "platform", {
      configurable: true,
      value: mode === "unsupported" ? "darwin" : "linux",
    });
    process.env.PATH = "/fixture-bin";
    process.env.XDG_DATA_HOME = join(home, ".local/share");
    process.env.XDG_CONFIG_HOME = join(home, ".config");
    process.env.XDG_CACHE_HOME = join(home, ".cache");
    fs.accessSync = (path, permission) => {
      if (
        [
          "/fixture-bin/systemctl",
          "/fixture-bin/systemd-analyze",
          "/fixture-bin/ffmpeg",
        ].includes(path)
      ) {
        if (mode === "missing-ffmpeg" && path.endsWith("/ffmpeg"))
          throw Error("missing ffmpeg");
        return;
      }
      return original.access(path, permission);
    };
    cp.spawnSync = (command, args, options) => {
      if (command === process.execPath) {
        actions.push("package-check");
        return original.spawn(command, args, options);
      }
      if (command === "/fixture-bin/ffmpeg")
        return { status: 0, stdout: "ffmpeg fixture" };
      if (command === "/fixture-bin/systemd-analyze") {
        actions.push("validate");
        assert.equal(active, hadPrevious);
        if (hadPrevious)
          assert.equal(fs.readFileSync(unit, "utf8"), "previous unit");
        assert.ok(fs.readFileSync(args[2], "utf8").includes("ExecStart=:"));
        return {
          status: mode === "validation" ? 1 : 0,
          stderr: "invalid unit",
        };
      }
      assert.equal(command, "/fixture-bin/systemctl");
      assert.equal(args[0], "--user");
      const action = args[1];
      if (action === "show")
        return {
          status: mode === "manager" ? 1 : 0,
          stderr: "user manager unavailable",
          stdout:
            mode === "limits" && args.includes("--property=ControlGroup")
              ? "/fixture-cd"
              : "262",
        };
      if (action === "is-active") return { status: active ? 0 : 3 };
      if (action === "is-enabled") return { status: enabled ? 0 : 1 };
      actions.push(action);
      if (action === "stop") active = false;
      if (action === "disable") {
        enabled = false;
        if (args.includes("--now")) active = false;
      }
      if (action === "enable") enabled = true;
      if (action === "restart" || action === "start") {
        ++starts;
        if (
          mode === "rollback" ||
          (["startup", "fresh-startup"].includes(mode) && starts === 1)
        )
          return { status: 1, stderr: "startup failed" };
        active = true;
      }
      return { status: 0 };
    };
    syncBuiltinESMExports();
    process.argv = [
      process.execPath,
      "install.mjs",
      mode === "uninstall" ? "uninstall" : "install",
    ];
    if (mode === "custom-config") process.argv.push("--config", configuration);
    let held;
    if (mode === "busy") {
      held = createServer();
      await new Promise((resolve) => held.listen(12890, "0.0.0.0", resolve));
    }
    process.exitCode = 0;
    try {
      await import(
        new URL(
          `../../../../../tools/cd/recognition/install.mjs?${mode}`,
          import.meta.url,
        )
      );
    } finally {
      if (held) await new Promise((resolve) => held.close(resolve));
    }
    const success = [
      "fresh",
      "reinstall",
      "custom-config",
      "limits",
      "uninstall",
    ].includes(mode);
    assert.equal(process.exitCode, success ? 0 : 1, mode);
    assert.equal(
      warnings.length,
      ["fresh", "reinstall", "custom-config"].includes(mode) ? 1 : 0,
      mode,
    );
    assert.equal(
      fs.readFileSync(join(cache, "retained.json"), "utf8"),
      "cached labels",
    );
    assert.equal(
      fs.readFileSync(configuration, "utf8"),
      previousConfiguration,
      "Installation and uninstall preserve the live configuration",
    );
    if (mode === "uninstall") {
      assert.equal(fs.existsSync(unit), false);
      assert.equal(fs.existsSync(application), false);
      assert.equal(active, false);
      assert.equal(enabled, false);
    } else if (success) {
      assert.equal(active, true);
      assert.equal(enabled, true);
      const text = fs.readFileSync(unit, "utf8");
      assert.ok(text.includes(process.execPath));
      assert.ok(text.includes('space%%$\\"'));
      assert.ok(text.includes('"--config"'));
      assert.ok(!text.includes(settings.contact));
      assert.ok(text.includes("MemoryMax=1G"));
      assert.ok(text.includes("CPUQuota=100%"));
      assert.ok(text.includes("KillMode=mixed"));
      assert.ok(!/^User=/mu.test(text));
      assert.ok(!text.includes('"--monitor"'));
      assert.ok(
        fs.existsSync(join(application, "tools/cd/recognition/config.json")),
      );
      assert.ok(fs.existsSync(join(application, "shared/contracts/cd.json")));
      assert.ok(
        fs.existsSync(
          join(application, "tools/cd/recognition/artwork-matching.mjs"),
        ),
      );
      assert.ok(!fs.existsSync(join(application, "previous.txt")));
      assert.ok(actions.indexOf("validate") < actions.indexOf("restart"));
    } else if (mode !== "rollback") {
      assert.equal(fs.existsSync(unit), hadPrevious, mode);
      if (hadPrevious) {
        assert.equal(fs.readFileSync(unit, "utf8"), "previous unit", mode);
        assert.equal(
          fs.readFileSync(join(application, "previous.txt"), "utf8"),
          "previous application",
          mode,
        );
      } else assert.equal(fs.existsSync(application), false, mode);
      assert.equal(active, hadPrevious, mode);
      assert.equal(enabled, hadPrevious, mode);
    }
    if (mode === "validation")
      assert.deepEqual(actions, ["package-check", "validate"]);
    if (
      [
        "missing-contact",
        "invalid-contact",
        "missing-ffmpeg",
        "manager",
        "busy",
        "unsupported",
        "root",
        "unmanaged",
      ].includes(mode)
    )
      assert.deepEqual(actions, []);
    if (mode !== "rollback") {
      for (const parent of [units, join(home, ".local/share/stroom")])
        if (fs.existsSync(parent))
          assert.ok(
            !fs
              .readdirSync(parent)
              .some((name) => name.startsWith(".cd-recognition-install-")),
          );
    }
  }
  console.log(
    "PASS: user installation, packaged dependencies, argument quoting, reinstall rollback, validation, port conflicts, and uninstall retaining cache.",
  );
} finally {
  cp.spawnSync = original.spawn;
  fs.accessSync = original.access;
  fs.readFileSync = original.read;
  console.warn = original.warn;
  os.homedir = original.home;
  syncBuiltinESMExports();
  process.argv = original.argv;
  process.getuid = original.uid;
  Object.defineProperty(process, "platform", original.platform);
  for (const [key, value] of Object.entries(original.env)) {
    if (value === undefined) delete process.env[key];
    else process.env[key] = value;
  }
  process.exitCode = original.exitCode;
  fs.rmSync(root, { recursive: true, force: true });
}
