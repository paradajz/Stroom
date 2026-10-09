import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { once } from "node:events";
import {
  mkdtemp,
  mkdir,
  writeFile,
  readFile,
  readdir,
  rm,
  access,
} from "node:fs/promises";
import { join, relative } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";
import { setTimeout as sleep } from "node:timers/promises";

const [binary] = process.argv.slice(2);
const directory = await mkdtemp(join(tmpdir(), "stroom-listener-install-"));
const installer = fileURLToPath(
  new URL("../../../../../tools/cd/listener/install.mjs", import.meta.url),
);
const unitName = "stroom-cd-listener.service";
const commands = join(directory, "commands");
const statePath = join(directory, "manager.json");
const data = join(directory, 'user data % " $');
const configuration = join(directory, 'user config % " $');
const application = join(data, "stroom/cd-listener");
const unit = join(configuration, "systemd/user", unitName);
const installed = join(application, "stroom-cd-listener");
const environment = {
  ...process.env,
  PATH: commands + ":" + process.env.PATH,
  XDG_DATA_HOME: data,
  XDG_CONFIG_HOME: configuration,
  STROOM_TEST_MANAGER: statePath,
};

if (process.getuid() === 0) {
  const result = spawnSync(process.execPath, [installer, "install", binary], {
    encoding: "utf8",
  });
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /Run as your login user/);
  console.log(
    "Root installation refusal passed; user-service installation checks require a non-root account",
  );
  await rm(directory, { recursive: true, force: true });
  process.exit(0);
}

/**
 * Run the production installer against an isolated systemd command substitute.
 * @param {string[]} args Installer action, binary and listener options.
 * @param {object} overrides Additional environment overrides.
 * @returns {object} Completed process status and captured output.
 */
function run(args, overrides = {}) {
  return spawnSync(process.execPath, [installer, ...args], {
    env: { ...environment, ...overrides },
    encoding: "utf8",
    timeout: 10000,
  });
}

/**
 * Read the substitute user manager's observed state and commands.
 * @returns {Promise<object>} Parsed manager state.
 */
async function manager() {
  return JSON.parse(await readFile(statePath, "utf8"));
}

/**
 * Check whether an installed path exists.
 * @param {string} path Filesystem path.
 * @returns {Promise<boolean>} True for an accessible path.
 */
async function exists(path) {
  try {
    await access(path);
    return true;
  } catch (error) {
    if (error.code === "ENOENT") return false;
    throw error;
  }
}

try {
  await mkdir(commands);
  await writeFile(
    statePath,
    JSON.stringify({ active: false, enabled: false, commands: [] }),
  );
  await writeFile(
    join(commands, "systemctl"),
    `#!${process.execPath}
const fs = require('node:fs');
const args = process.argv.slice(2);
if (args.shift() !== '--user') process.exit(99);
const path = process.env.STROOM_TEST_MANAGER;
const state = JSON.parse(fs.readFileSync(path, 'utf8'));
const command = args[0];
state.commands.push(args);
// daemon-reload does not load a newly installed, inactive unit.
const unloaded = command === 'reset-failed' && !state.loaded;
const fail = process.env.STROOM_TEST_FAIL === command && !state.failed;
if (fail) state.failed = true;
if (!fail) {
  if (command === 'stop') state.active = false;
  if (command === 'enable') state.enabled = true;
  if (command === 'disable') {
    state.enabled = false;
    if (args.includes('--now')) state.active = false;
  }
  if (command === 'start' || command === 'restart') {
    state.loaded = true;
    state.active = true;
  }
}
fs.writeFileSync(path, JSON.stringify(state));
if (unloaded) { console.error('Unit stroom-cd-listener.service not loaded.'); process.exit(1); }
if (fail) { console.error('Injected manager failure: ' + command); process.exit(1); }
if (command === 'is-active') process.exit(state.active ? 0 : 3);
if (command === 'is-enabled') process.exit(state.enabled ? 0 : 1);
if (command === process.env.STROOM_TEST_PAUSE) {
  fs.writeFileSync(process.env.STROOM_TEST_PAUSED, command);
  setTimeout(() => {}, 2000);
}
`,
    { mode: 0o755 },
  );

  let result = run(["install", binary], { STROOM_TEST_FAIL: "restart" });
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /Injected manager failure: restart/);
  assert.equal(await exists(unit), false);
  assert.equal(await exists(application), false);
  assert.equal((await manager()).active, false);
  assert.equal((await manager()).enabled, false);
  await writeFile(
    statePath,
    JSON.stringify({ active: false, enabled: false, commands: [] }),
  );

  result = run(["install", binary]);
  assert.equal(result.status, 0, result.stderr);
  const originalUnit = await readFile(unit, "utf8");
  const originalBinary = await readFile(installed);
  assert.deepEqual(originalBinary, await readFile(binary));
  assert.match(originalUnit, /Wants=pipewire\.socket/);
  assert.match(originalUnit, /Restart=always\nRestartSec=10/);
  assert.match(originalUnit, /StartLimitIntervalSec=0/);
  assert.match(originalUnit, /TimeoutStopSec=10/);
  assert.match(originalUnit, /SuccessExitStatus=130 143/);
  assert.match(originalUnit, /LogRateLimitBurst=100/);
  const quoted =
    '"' +
    installed
      .replaceAll("\\", "\\\\")
      .replaceAll('"', '\\"')
      .replaceAll("%", "%%") +
    '"';
  const settings = join(configuration, "stroom/listener.json");
  const quotedSettings =
    '"' +
    settings
      .replaceAll("\\", "\\\\")
      .replaceAll('"', '\\"')
      .replaceAll("%", "%%") +
    '"';
  assert(
    originalUnit.includes(`ExecStart=:${quoted} "--config" ${quotedSettings}`),
    originalUnit,
  );
  assert.doesNotMatch(
    originalUnit,
    /RestrictRealtime=yes|CPUSchedulingPolicy=|LimitRTPRIO=|CPUQuota=/,
  );
  assert.equal((await manager()).active, true);
  assert.equal((await manager()).enabled, true);

  // Argument failures are detected before stopping an existing listener.
  for (const args of [
    ["--once"],
    ["--list"],
    ["--help"],
    ["--check-config"],
    ["--port", "0"],
    ["--buffer-ms", "2001"],
    ["--host", "invalid"],
  ]) {
    const count = (await manager()).commands.length;
    result = run(["install", binary, ...args]);
    assert.notEqual(result.status, 0, args.join(" "));
    assert.equal(await readFile(unit, "utf8"), originalUnit);
    assert.equal((await manager()).active, true);
    assert(
      !(await manager()).commands
        .slice(count)
        .some((args) => args[0] === "stop"),
    );
  }

  // Inject failures at each service mutation and preserve the installed version.
  const initialSettings = await readFile(settings);
  await writeFile(settings, "{ invalid");
  result = run(["install", binary]);
  assert.notEqual(result.status, 0);
  assert.equal(await readFile(unit, "utf8"), originalUnit);
  assert.equal((await manager()).active, true);
  await writeFile(settings, initialSettings);

  const candidate = join(directory, "candidate");
  await writeFile(
    candidate,
    Buffer.concat([originalBinary, Buffer.from("\nlistener-update-test\n")]),
    { mode: 0o755 },
  );
  for (const command of ["stop", "daemon-reload", "enable", "restart"]) {
    const state = await manager();
    delete state.failed;
    await writeFile(statePath, JSON.stringify(state));
    result = run(["install", candidate], {
      STROOM_TEST_FAIL: command,
    });
    assert.notEqual(result.status, 0, command);
    assert.match(result.stderr, /installed files and service state restored/);
    assert.equal(await readFile(unit, "utf8"), originalUnit, command);
    assert.deepEqual(await readFile(installed), originalBinary, command);
    assert.equal((await manager()).active, true, command);
    assert.equal((await manager()).enabled, true, command);
    assert(
      !(await readdir(join(data, "stroom"), { withFileTypes: true })).some(
        (entry) =>
          entry.isDirectory() && entry.name.startsWith(".cd-listener-install."),
      ),
    );
  }

  // A failed update must also preserve an intentionally stopped, disabled unit.
  await writeFile(
    statePath,
    JSON.stringify({ active: false, enabled: false, commands: [] }),
  );
  result = run(["install", candidate], { STROOM_TEST_FAIL: "restart" });
  assert.notEqual(result.status, 0);
  assert.equal((await manager()).active, false);
  assert.equal((await manager()).enabled, false);
  assert.equal(await readFile(unit, "utf8"), originalUnit);
  assert.deepEqual(await readFile(installed), originalBinary);

  const customSettings = join(directory, "selected listener.json");
  await writeFile(customSettings, '{"buffer_ms":750}');
  result = run([
    "install",
    candidate,
    "--config",
    relative(process.cwd(), customSettings),
  ]);
  assert.equal(result.status, 0, result.stderr);
  assert.deepEqual(await readFile(installed), await readFile(candidate));
  assert.doesNotMatch(
    await readFile(unit, "utf8"),
    /--buffer-ms|--host|--port|--broadcast/,
  );
  assert((await readFile(unit, "utf8")).includes(customSettings));

  // Hold an update after stop, reject a competing installer, then interrupt it.
  const currentUnit = await readFile(unit, "utf8");
  const paused = join(directory, "paused");
  const updating = spawn(process.execPath, [installer, "install", candidate], {
    env: {
      ...environment,
      STROOM_TEST_PAUSE: "stop",
      STROOM_TEST_PAUSED: paused,
    },
    stdio: ["ignore", "ignore", "pipe"],
  });
  const finished = once(updating, "close");
  let updateError = "";
  updating.stderr.on("data", (data) => {
    updateError += data;
  });
  try {
    const deadline = Date.now() + 5000;
    while (!(await exists(paused))) {
      assert.equal(updating.exitCode, null, updateError);
      assert(Date.now() < deadline, "Update did not reach stop");
      await sleep(20);
    }
    result = run(["install", candidate]);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /Another listener installation is running/);
    updating.kill("SIGTERM");
    assert.equal((await finished)[0], 143, updateError);
    assert.match(updateError, /installed files and service state restored/);
    assert.equal(await readFile(unit, "utf8"), currentUnit);
    assert.deepEqual(await readFile(installed), await readFile(candidate));
    assert.equal((await manager()).active, true);
    assert.equal((await manager()).enabled, true);
  } finally {
    if (updating.exitCode === null) updating.kill("SIGTERM");
    await finished;
  }

  result = run(["uninstall"]);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(await exists(unit), false);
  assert.equal(await exists(application), false);
  assert.equal(await exists(settings), true);
  assert.equal(await readFile(customSettings, "utf8"), '{"buffer_ms":750}');
  assert.equal((await manager()).active, false);
  assert.equal((await manager()).enabled, false);

  // Unmanaged files remain available for the operator to inspect.
  await writeFile(unit, "[Service]\nExecStart=/usr/bin/custom-listener\n");
  result = run(["install", binary]);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /unmanaged unit/);
  assert.match(await readFile(unit, "utf8"), /custom-listener/);
  await rm(unit);
  await mkdir(application);
  await writeFile(join(application, "keep"), "operator file");
  result = run(["uninstall"]);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /unmanaged installation/);
  assert.equal(
    await readFile(join(application, "keep"), "utf8"),
    "operator file",
  );
  console.log(
    "Listener installation, literal unit paths, updates, rollback, concurrent locking, interrupted updates, validation and managed uninstall passed",
  );
} finally {
  await rm(directory, { recursive: true, force: true });
}
