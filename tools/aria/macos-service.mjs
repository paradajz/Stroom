#!/usr/bin/env node
import { metadata } from "../contracts/load.mjs";
import { spawnSync } from "node:child_process";
import {
  mkdirSync,
  mkdtempSync,
  copyFileSync,
  renameSync,
  writeFileSync,
  rmSync,
  existsSync,
} from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { validDeviceName } from "./ariacast.mjs";

const usage = `Usage: node tools/aria/macos-service.mjs COMMAND [HOST [DEVICE_NAME]]
  install    Install and start at login (default: 192.168.1.240, Focusrite 18i20)
  start      Start an installed agent
  stop       Stop until the next login or start command
  status     Show launchd status
  uninstall  Stop and remove the agent; retain logs
Requires macOS, FFmpeg on PATH, and a Scarlett 18i20 3rd gen.`;

function run(command, args, options = {}) {
  const result = spawnSync(command, args, { encoding: "utf8", ...options });
  if (result.error) throw result.error;
  if (result.status !== 0)
    throw new Error(
      result.stderr?.trim() || `${command} exited ${result.status}`,
    );
  return result.stdout?.trim();
}

function xml(value) {
  return value.replace(
    /[&<>"']/g,
    (character) =>
      ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&apos;",
      })[character],
  );
}

try {
  const [action, host = "192.168.1.240", deviceName = "Focusrite 18i20"] =
    process.argv.slice(2);
  if (!action || action === "--help") {
    console.log(usage);
  } else {
    if (
      !["install", "start", "stop", "status", "uninstall"].includes(action) ||
      process.argv.length > (action === "install" ? 5 : 3)
    )
      throw new Error(usage);
    if (process.platform !== "darwin")
      throw new Error("Run this command on the Mac.");
    if (process.getuid() === 0)
      throw new Error("Run as your logged-in user, without sudo.");

    const label = "local.stroom.listening";
    const domain = `gui/${process.getuid()}`;
    const service = `${domain}/${label}`;
    const agentDir = join(homedir(), "Library/LaunchAgents");
    const plist = join(agentDir, `${label}.plist`);
    const logDir = join(homedir(), "Library/Logs/stroom");
    const log = join(logDir, "sender.log");
    const loaded = () =>
      spawnSync("/bin/launchctl", ["print", service], { stdio: "ignore" })
        .status === 0;
    const stop = () => {
      if (loaded()) run("/bin/launchctl", ["bootout", service]);
    };
    const start = () => {
      if (!existsSync(plist)) throw new Error("Install the agent first.");
      if (!loaded()) run("/bin/launchctl", ["bootstrap", domain, plist]);
    };

    if (action === "install") {
      if (!host || /[\s\x00-\x1f]/u.test(host))
        throw new Error("Supply a hostname or IP address.");
      if (!validDeviceName(deviceName))
        throw new Error(
          `Device name must be 1–${metadata.METADATA_DEVICE_NAME_MAX_BYTES} UTF-8 bytes without controls.`,
        );
      const ffmpeg = run("/usr/bin/which", ["ffmpeg"]);
      const runner = fileURLToPath(
        new URL("./macos-listen.mjs", import.meta.url),
      );
      const args = [process.execPath, runner, ffmpeg, host, deviceName, log];
      const contents = `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>${label}</string>
  <key>ProgramArguments</key><array>
    ${args.map((arg) => `<string>${xml(arg)}</string>`).join("\n    ")}
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>5</integer>
  <key>StandardOutPath</key><string>/dev/null</string>
  <key>StandardErrorPath</key><string>/dev/null</string>
</dict></plist>
`;
      mkdirSync(agentDir, { recursive: true });
      mkdirSync(logDir, { recursive: true });
      const staging = mkdtempSync(join(agentDir, ".stroom-install-"));
      const replacement = join(staging, "replacement.plist");
      const previous = join(staging, "previous.plist");
      let keepBackup = false;
      try {
        writeFileSync(replacement, contents, { mode: 0o600 });
        run("/usr/bin/plutil", ["-lint", replacement]);
        const hadPrevious = existsSync(plist);
        if (hadPrevious) copyFileSync(plist, previous);
        const wasLoaded = loaded();
        // Validate and save the replacement before touching the installed job.
        stop();
        try {
          renameSync(replacement, plist);
          start();
        } catch (error) {
          try {
            stop();
            if (hadPrevious) {
              copyFileSync(previous, replacement);
              renameSync(replacement, plist);
            } else rmSync(plist, { force: true });
            if (wasLoaded) start();
          } catch (rollbackError) {
            keepBackup = true;
            throw new Error(
              `${error.message}; rollback failed: ${rollbackError.message}. Recovery files: ${staging}`,
            );
          }
          throw new Error(`${error.message}; previous installation restored.`);
        }
      } finally {
        if (!keepBackup) rmSync(staging, { recursive: true, force: true });
      }
      console.log(
        `Installed and started. Log: ${log}\nVerify the PS2 meters: microphone permission must work in the agent's launch context.`,
      );
    } else if (action === "start") {
      start();
      console.log("Started.");
    } else if (action === "stop" || action === "uninstall") {
      stop();
      if (action === "uninstall") rmSync(plist, { force: true });
      console.log(
        action === "stop"
          ? "Stopped until next login or start."
          : "Uninstalled; logs retained.",
      );
    } else {
      run("/bin/launchctl", ["print", service], { stdio: "inherit" });
    }
  }
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
