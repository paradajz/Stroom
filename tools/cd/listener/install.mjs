#!/usr/bin/env node
import { execFile, spawnSync } from "node:child_process";
import {
  accessSync,
  chmodSync,
  closeSync,
  constants,
  copyFileSync,
  existsSync,
  lstatSync,
  mkdirSync,
  mkdtempSync,
  openSync,
  readFileSync,
  renameSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { homedir } from "node:os";
import { isAbsolute, join, resolve } from "node:path";
import { promisify } from "node:util";

const execute = promisify(execFile);
const unitName = "stroom-cd-listener.service";
const marker = "Managed by Stroom CD listener installer";
const usage = `Usage: node tools/cd/listener/install.mjs install BINARY [--config FILE]
       node tools/cd/listener/install.mjs uninstall
Installs and starts a systemd user service for your login account.`;
let shutdownSignal = 0;
let recovering = false;

/**
 * Abort ordinary installation work after a shutdown request.
 * @throws {Error} When a signal has requested cancellation outside rollback.
 */
function cancelled() {
  if (shutdownSignal && !recovering) throw Error("Installation interrupted.");
}

/**
 * Run a bounded child command without invoking a shell.
 * @param {string} command Executable name or path.
 * @param {string[]} args Literal command arguments.
 * @returns {Promise<string>} Trimmed standard output.
 */
async function run(command, args) {
  cancelled();
  const result = await execute(command, args, {
    encoding: "utf8",
    timeout: 180000,
    killSignal: "SIGKILL",
  });
  cancelled();
  return result.stdout.trim();
}

/**
 * Run one command against the current account's systemd user manager.
 * @param {...string} args Arguments after --user.
 * @returns {Promise<string>} Trimmed systemctl output.
 */
function control(...args) {
  return run("systemctl", ["--user", ...args]);
}

/**
 * Read whether the installed service is active or enabled.
 * @param {string} property systemctl state query, such as is-active.
 * @returns {Promise<boolean>} True when the query exits successfully.
 */
async function state(property) {
  try {
    await control(property, "--quiet", unitName);
    return true;
  } catch (error) {
    cancelled();
    if (typeof error.code === "number") return false;
    throw error;
  }
}

/**
 * Quote a literal systemd argument with percent specifiers escaped.
 * @param {string} value Argument text; control characters are rejected.
 * @returns {string} Double-quoted argument for an ExecStart with ':' prefix.
 */
function quote(value) {
  if (/[\x00-\x1f\x7f]/u.test(value))
    throw Error("Paths and arguments cannot contain control characters.");
  return (
    '"' +
    value
      .replaceAll("\\", "\\\\")
      .replaceAll('"', '\\"')
      .replaceAll("%", "%%") +
    '"'
  );
}

/**
 * Select an absolute XDG directory or the account's default directory.
 * @param {string} name Environment variable name.
 * @param {string} fallback Default absolute directory.
 * @returns {string} Selected directory.
 */
function xdg(name, fallback) {
  return isAbsolute(process.env[name] ?? "") ? process.env[name] : fallback;
}

/**
 * Read metadata without following a final symlink.
 * @param {string} path Filesystem path to inspect.
 * @returns {object|null} Metadata, or null when the path does not exist.
 */
function metadata(path) {
  try {
    return lstatSync(path);
  } catch (error) {
    if (error.code === "ENOENT") return null;
    throw error;
  }
}

/**
 * Generate the continuous desktop-audio service unit.
 * @param {string} binary Installed executable path.
 * @param {string[]} args Saved listener arguments.
 * @returns {string} Complete unit file.
 */
function contents(binary, args) {
  return `# ${marker}
[Unit]
Description=Stroom CD listener
Wants=pipewire.socket
After=pipewire.socket
StartLimitIntervalSec=0

[Service]
Type=exec
ExecStart=:${[binary, ...args].map(quote).join(" ")}
Restart=always
RestartSec=10
TimeoutStopSec=10
KillMode=control-group
SuccessExitStatus=130 143
NoNewPrivileges=yes
UMask=0077
StandardOutput=journal
StandardError=journal
LogRateLimitIntervalSec=30s
LogRateLimitBurst=100

[Install]
WantedBy=default.target
`;
}

/**
 * Install, update or remove the current account's managed listener service.
 * @returns {Promise<void>} Resolves after the requested service action completes.
 */
async function main() {
  const [action, requestedBinary, ...args] = process.argv.slice(2);
  if (["--help", "-h"].includes(action)) {
    console.log(usage);
    return;
  }
  if (!["install", "uninstall"].includes(action)) throw Error(usage);
  if (process.platform !== "linux")
    throw Error("Installation requires Linux and systemd.");
  if (process.getuid() === 0)
    throw Error("Run as your login user, without sudo.");
  if (action === "uninstall" && requestedBinary !== undefined)
    throw Error("Uninstall accepts no listener options.");
  if (action === "install" && !requestedBinary)
    throw Error("Install requires the built listener executable.");

  process.umask(0o077);
  process.on("SIGHUP", () => {
    shutdownSignal = 1;
  });
  process.on("SIGINT", () => {
    shutdownSignal = 2;
  });
  process.on("SIGTERM", () => {
    shutdownSignal = 15;
  });
  await control("show-environment");
  const home = homedir();
  const parent = join(
    xdg("XDG_DATA_HOME", join(home, ".local/share")),
    "stroom",
  );
  const application = join(parent, "cd-listener");
  const units = join(
    xdg("XDG_CONFIG_HOME", join(home, ".config")),
    "systemd/user",
  );
  const unit = join(units, unitName);
  mkdirSync(parent, { recursive: true });
  mkdirSync(units, { recursive: true });

  // flock's child and this process share the open file description. The lock
  // remains held by our descriptor until the install/uninstall finishes.
  const lock = openSync(join(parent, ".cd-listener-install.lock"), "a", 0o600);
  try {
    const acquired = spawnSync("flock", ["-n", "3"], {
      stdio: ["ignore", "pipe", "pipe", lock],
      encoding: "utf8",
      timeout: 5000,
    });
    if (acquired.error) throw acquired.error;
    if (acquired.status !== 0)
      throw Error(
        acquired.status === 1
          ? "Another listener installation is running."
          : acquired.stderr.trim() || "Cannot lock the listener installation.",
      );
    cancelled();
    const existingApplication = metadata(application);
    if (
      existingApplication &&
      (!existingApplication.isDirectory() ||
        !existsSync(join(application, "installation")) ||
        readFileSync(join(application, "installation"), "utf8").trim() !==
          marker)
    )
      throw Error(
        `Refusing to change an unmanaged installation: ${application}`,
      );
    const existingUnit = metadata(unit);
    if (
      existingUnit &&
      (!existingUnit.isFile() ||
        readFileSync(unit, "utf8").split("\n")[0] !== `# ${marker}`)
    )
      throw Error(`Refusing to change an unmanaged unit: ${unit}`);

    if (action === "uninstall") {
      if (existingUnit) {
        await control("disable", "--now", unitName);
        rmSync(unit);
        await control("daemon-reload");
      }
      rmSync(application, { recursive: true, force: true });
      console.log("CD listener service uninstalled.");
      return;
    }

    const binary = resolve(requestedBinary);
    accessSync(binary, constants.X_OK);
    if (!statSync(binary).isFile())
      throw Error(`Not an executable file: ${binary}`);
    for (const argument of args)
      if (
        ["--list", "--help", "-h", "--check-config"].includes(
          argument.split("=")[0],
        )
      )
        throw Error(`A background listener cannot use ${argument}`);
    const installed = join(application, "stroom-cd-listener");
    const configuration = await run(binary, [...args, "--check-config"]);
    if (!isAbsolute(configuration))
      throw Error("Listener did not return an absolute configuration path");
    const savedArgs = ["--config", configuration];
    const text = contents(installed, savedArgs);
    const stage = mkdtempSync(join(parent, ".cd-listener-install."));
    let unitStage;
    let keepBackup = false;
    try {
      unitStage = mkdtempSync(join(units, ".cd-listener-install."));
      const replacement = join(stage, "application");
      const backup = join(stage, "previous");
      const replacementUnit = join(unitStage, unitName);
      const previousUnit = existingUnit ? readFileSync(unit) : null;
      mkdirSync(replacement);
      copyFileSync(binary, join(replacement, "stroom-cd-listener"));
      chmodSync(join(replacement, "stroom-cd-listener"), 0o700);
      writeFileSync(join(replacement, "installation"), marker + "\n");
      writeFileSync(replacementUnit, text, { mode: 0o600 });
      if (previousUnit)
        writeFileSync(join(unitStage, "previous.service"), previousUnit, {
          mode: 0o600,
        });
      await run(join(replacement, "stroom-cd-listener"), [
        ...savedArgs,
        "--check-config",
      ]);
      const active = await state("is-active");
      const enabled = await state("is-enabled");
      let replaced = false;
      let unitReplaced = false;
      try {
        if (existingUnit) await control("stop", unitName);
        cancelled();
        if (existingApplication) renameSync(application, backup);
        renameSync(replacement, application);
        replaced = true;
        renameSync(replacementUnit, unit);
        unitReplaced = true;
        await control("daemon-reload");
        await control("enable", unitName);
        await control("restart", unitName);
      } catch (error) {
        recovering = true;
        const failures = [];
        /**
         * Attempt a recovery step while retaining failures for the final report.
         * @param {Function} operation Synchronous or asynchronous recovery step.
         * @returns {Promise<void>} Resolves after recording any failure.
         */
        const restore = async (operation) => {
          try {
            await operation();
          } catch (failure) {
            failures.push(failure);
          }
        };
        if (unitReplaced) {
          await restore(() => control("stop", unitName));
          if (!previousUnit) await restore(() => control("disable", unitName));
        }
        await restore(() => {
          if (previousUnit) {
            writeFileSync(replacementUnit, previousUnit, { mode: 0o600 });
            renameSync(replacementUnit, unit);
          } else if (unitReplaced) rmSync(unit);
        });
        await restore(() => {
          if (replaced) rmSync(application, { recursive: true, force: true });
        });
        await restore(() => {
          if (existsSync(backup)) renameSync(backup, application);
        });
        await restore(() => control("daemon-reload"));
        if (previousUnit)
          await restore(() =>
            control(enabled ? "enable" : "disable", unitName),
          );
        if (active) await restore(() => control("start", unitName));
        if (failures.length) {
          keepBackup = true;
          throw Error(
            `${error.message}; recovery needs attention: ${failures.map((failure) => failure.message).join("; ")}. Files retained in ${stage} and ${unitStage}`,
          );
        }
        throw Error(
          `${error.message}; installed files and service state restored.`,
        );
      }
    } finally {
      if (!keepBackup) {
        rmSync(stage, { recursive: true, force: true });
        if (unitStage) rmSync(unitStage, { recursive: true, force: true });
      }
    }
    console.log(
      `Installed and started ${unitName} under your account.\nExecutable: ${installed}\nConfiguration: ${configuration}\nStatus: systemctl --user status ${unitName}\nLogs: journalctl --user -u ${unitName} -f`,
    );
  } finally {
    closeSync(lock);
  }
}

try {
  await main();
} catch (error) {
  console.error("CD listener installation: " + error.message);
  process.exitCode = shutdownSignal ? 128 + shutdownSignal : 1;
}
