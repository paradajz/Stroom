#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import {
  accessSync,
  constants,
  copyFileSync,
  cpSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  readdirSync,
  renameSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { createServer } from "node:net";
import { homedir } from "node:os";
import { dirname, isAbsolute, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import { cd } from "../../contracts/load.mjs";
import { loadConfig } from "./config.mjs";

const unitName = "stroom-cd-recognition.service";
const usage = `Usage: node tools/cd/recognition/install.mjs install [--config FILE]
       node tools/cd/recognition/install.mjs uninstall
Installs a Linux systemd user service under your current account, without sudo.
Uses the current Node executable, FFmpeg on PATH and your existing CD cache.
Reinstall after updating the service or switching Node installations.
Uninstall retains configuration, cached results and journal records.`;

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    encoding: "utf8",
    timeout: 180000,
    ...options,
  });
  if (result.error) throw result.error;
  if (result.status !== 0)
    throw Error(result.stderr?.trim() || `${command} exited ${result.status}`);
  return result.stdout?.trim();
}

function executable(name) {
  for (const directory of (process.env.PATH ?? "").split(":")) {
    const path = resolve(directory || ".", name);
    try {
      accessSync(path, constants.X_OK);
      return path;
    } catch {
      /* Try the next PATH entry. */
    }
  }
  throw Error(`${name} must be installed and on PATH.`);
}

// systemd parses its own quoting and %-specifiers. ExecStart's ':' prefix
// disables $-expansion; no shell is involved.
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

function xdg(name, fallback) {
  return isAbsolute(process.env[name] ?? "") ? process.env[name] : fallback;
}

async function checkPort() {
  await new Promise((resolve, reject) => {
    const server = createServer();
    server.once("error", (error) =>
      reject(
        error.code === "EADDRINUSE"
          ? Error(
              `TCP ${cd.CD_LOOKUP_PORT} is already in use. Stop any foreground make cd-recognize process, then retry.`,
            )
          : error,
      ),
    );
    server.listen(cd.CD_LOOKUP_PORT, "0.0.0.0", () => server.close(resolve));
  });
}

function contents(application, configuration, ffmpeg) {
  const args = [
    process.execPath,
    join(application, "tools/cd/recognition/service.mjs"),
    "--config",
    configuration,
  ];
  const path = [
    ...new Set([
      dirname(ffmpeg),
      dirname(process.execPath),
      "/usr/local/bin",
      "/usr/bin",
      "/bin",
    ]),
  ].join(":");
  return `[Unit]
Description=Stroom CD recognition
StartLimitIntervalSec=300
StartLimitBurst=5

[Service]
Type=exec
ExecStart=:${args.map(quote).join(" ")}
Environment=${quote("PATH=" + path)}
Environment=${quote("XDG_CACHE_HOME=" + xdg("XDG_CACHE_HOME", join(homedir(), ".cache")))}
Restart=on-failure
RestartSec=5
TimeoutStopSec=150
KillMode=mixed
OOMPolicy=kill
MemoryAccounting=yes
MemoryHigh=512M
MemoryMax=1G
MemorySwapMax=0
CPUQuota=100%
TasksMax=128
NoNewPrivileges=yes
UMask=0077
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=default.target
`;
}

async function main() {
  const { values, positionals } = parseArgs({
    allowPositionals: true,
    options: {
      config: { type: "string" },
      help: { type: "boolean", short: "h" },
    },
  });
  if (values.help) {
    console.log(usage);
    return;
  }
  const [action] = positionals;
  if (positionals.length !== 1 || !["install", "uninstall"].includes(action))
    throw Error(usage);
  if (process.platform !== "linux")
    throw Error(
      "Automatic installation requires Linux with systemd. Use make cd-recognize to run in a terminal.",
    );
  if (process.getuid() === 0)
    throw Error("Run as your login user, without sudo.");
  const requestedConfig =
    values.config ?? (process.env.STROOM_CD_CONFIG || undefined);
  const configuration =
    action === "install"
      ? loadConfig(requestedConfig, { initialize: !requestedConfig })
      : null;
  const home = homedir();
  const units = join(
    xdg("XDG_CONFIG_HOME", join(home, ".config")),
    "systemd/user",
  );
  const parent = join(
    xdg("XDG_DATA_HOME", join(home, ".local/share")),
    "stroom",
  );
  const application = join(parent, "cd-recognition");
  const unit = join(units, unitName);
  const root = resolve(fileURLToPath(new URL("../../../", import.meta.url)));
  const inside = (path) =>
    path === application || path.startsWith(application + "/");
  if (inside(root))
    throw Error(
      "The installation directory must be separate from the checkout.",
    );
  if (existsSync(application)) {
    let owned = false;
    try {
      owned =
        JSON.parse(readFileSync(join(application, "installation.json"), "utf8"))
          .service === unitName;
    } catch {
      /* Not an installed application. */
    }
    if (!owned)
      throw Error(
        `Refusing to replace or remove an unmanaged directory: ${application}`,
      );
  }
  const systemctl = executable("systemctl");
  const control = (...args) => run(systemctl, ["--user", ...args]);
  control("show", "--property=Version", "--value"); // Check the user manager before any mutation.

  if (action === "uninstall") {
    if (existsSync(unit)) {
      control("disable", "--now", unitName);
      rmSync(unit);
      control("daemon-reload");
    }
    rmSync(application, { recursive: true, force: true });
    console.log(
      "CD recognition uninstalled; configuration, cache and journal records retained.",
    );
    return;
  }

  if (Number(process.versions.node.split(".")[0]) < 24)
    throw Error("Use Node.js 24 or newer to install recognition.");
  const ffmpeg = executable("ffmpeg");
  run(ffmpeg, ["-version"]);
  const { config, path: configurationPath } = configuration;
  const output = config.cache_directory;
  if (inside(output))
    throw Error(
      "The installation directory must be separate from the checkout and cache.",
    );
  if (inside(configurationPath))
    throw Error(
      "The configuration file must be outside the installation directory.",
    );
  const text = contents(application, configurationPath, ffmpeg);
  const state = (property) =>
    spawnSync(systemctl, ["--user", property, "--quiet", unitName], {
      stdio: "ignore",
    }).status === 0;
  const active = state("is-active"),
    enabled = state("is-enabled");
  if (!active) await checkPort();
  mkdirSync(parent, { recursive: true });
  mkdirSync(units, { recursive: true });
  const stage = mkdtempSync(join(parent, ".cd-recognition-install-"));
  const unitStage = mkdtempSync(join(units, ".cd-recognition-install-"));
  let keepBackup = false;
  try {
    const replacement = join(stage, "application");
    const backup = join(stage, "previous");
    const replacementUnit = join(unitStage, unitName);
    const previousUnit = existsSync(unit) ? readFileSync(unit) : null;
    mkdirSync(join(replacement, "tools/cd/recognition"), { recursive: true });
    mkdirSync(join(replacement, "tools/contracts"), { recursive: true });
    for (const name of readdirSync(join(root, "tools/cd/recognition")))
      if (
        (name.endsWith(".mjs") && name !== "install.mjs") ||
        name === "config.json"
      )
        copyFileSync(
          join(root, "tools/cd/recognition", name),
          join(replacement, "tools/cd/recognition", name),
        );
    copyFileSync(
      join(root, "tools/contracts/load.mjs"),
      join(replacement, "tools/contracts/load.mjs"),
    );
    cpSync(
      join(root, "shared/contracts"),
      join(replacement, "shared/contracts"),
      { recursive: true },
    );
    writeFileSync(
      join(replacement, "installation.json"),
      JSON.stringify({ service: unitName }) + "\n",
    );
    writeFileSync(replacementUnit, text, { mode: 0o600 });
    if (previousUnit)
      writeFileSync(join(unitStage, "previous.service"), previousUnit, {
        mode: 0o600,
      });
    run(process.execPath, [
      join(replacement, "tools/cd/recognition/service.mjs"),
      "--help",
    ]);
    let analyze;
    try {
      analyze = executable("systemd-analyze");
    } catch {
      /* Optional syntax check. */
    }
    if (analyze) run(analyze, ["--user", "verify", replacementUnit]);

    let replaced = false,
      unitReplaced = false;
    try {
      if (existsSync(unit)) control("stop", unitName);
      if (existsSync(application)) renameSync(application, backup);
      renameSync(replacement, application);
      replaced = true;
      renameSync(replacementUnit, unit);
      unitReplaced = true;
      control("daemon-reload");
      control("enable", unitName);
      control("restart", unitName);
    } catch (error) {
      try {
        if (unitReplaced) control("stop", unitName);
        if (unitReplaced) {
          if (previousUnit) {
            writeFileSync(replacementUnit, previousUnit, { mode: 0o600 });
            renameSync(replacementUnit, unit);
          } else {
            control("disable", unitName);
            rmSync(unit);
          }
        }
        if (replaced) rmSync(application, { recursive: true, force: true });
        if (existsSync(backup)) renameSync(backup, application);
        control("daemon-reload");
        if (previousUnit) control(enabled ? "enable" : "disable", unitName);
        if (active) control("start", unitName);
      } catch (rollback) {
        keepBackup = true;
        throw Error(
          `${error.message}; rollback failed: ${rollback.message}. Recovery files: ${stage} and ${unitStage}`,
        );
      }
      throw Error(`${error.message}; previous installation restored.`);
    }
  } finally {
    if (!keepBackup) {
      rmSync(stage, { recursive: true, force: true });
      rmSync(unitStage, { recursive: true, force: true });
    }
  }
  console.log(
    `Installed under your account and started; enabled at login.\nApplication: ${application}\nConfiguration: ${configurationPath}\nCache: ${output}\nStatus: systemctl --user status ${unitName}\nLogs: journalctl --user -u ${unitName} -f\nFor startup before login and continued use after logout, optionally run: loginctl enable-linger`,
  );
  try {
    const group = control(
      "show",
      unitName,
      "--property=ControlGroup",
      "--value",
    );
    if (!group.startsWith("/") || group.includes(".."))
      throw Error("Missing control group");
    const base = "/sys/fs/cgroup" + group;
    const value = (name) => readFileSync(join(base, name), "utf8").trim();
    const [quota, period] = value("cpu.max").split(/\s+/u).map(Number);
    if (
      value("memory.max") !== "1073741824" ||
      value("memory.high") !== "536870912" ||
      value("memory.swap.max") !== "0" ||
      value("pids.max") !== "128" ||
      quota !== period
    )
      throw Error(
        "Resource controllers are not applying the configured limits",
      );
  } catch {
    console.warn(
      "Resource limits could not be confirmed. The service is installed, but CPU/memory/task enforcement requires cgroup v2 controllers delegated to your systemd user manager. See tools/cd/recognition/README.md#install-as-a-background-service-linux.",
    );
  }
}

try {
  await main();
} catch (error) {
  console.error("CD recognition installation failed: " + error.message);
  process.exitCode = 1;
}
