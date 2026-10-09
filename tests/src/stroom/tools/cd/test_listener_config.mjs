import assert from "node:assert/strict";
import { execFile, spawn, spawnSync } from "node:child_process";
import {
  mkdtemp,
  mkdir,
  readFile,
  writeFile,
  stat,
  rm,
  access,
} from "node:fs/promises";
import net from "node:net";
import { once } from "node:events";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { promisify } from "node:util";

const [binary, fixture] = process.argv.slice(2);
const execute = promisify(execFile);
const directory = await mkdtemp(join(tmpdir(), "stroom-listener-config-"));
const home = join(directory, "home");
const xdg = join(directory, "xdg");
const configuration = join(xdg, "stroom/listener.json");
const environment = {
  ...process.env,
  HOME: home,
  XDG_CONFIG_HOME: xdg,
};

/**
 * Validate configuration with the production executable and isolated account paths.
 * @param {string[]} args Literal command-line arguments.
 * @param {object} env Per-call environment overrides.
 * @returns {object} Captured process result.
 */
function run(args = ["--check-config"], env = {}) {
  return spawnSync(binary, args, {
    env: { ...environment, ...env },
    cwd: directory,
    encoding: "utf8",
    timeout: 5000,
  });
}

/**
 * Exercise actual config-derived listening through the real CLI and transport.
 * @returns {Promise<string>} Listener output from a short, clean stream.
 */
async function listen() {
  const sockets = new Set();
  const service = net.createServer((socket) => {
    sockets.add(socket);
    socket.on("error", () => {});
    socket.once("close", () => sockets.delete(socket));
    socket.once("data", (request) => {
      assert.equal(request.toString(), "STROOM CD 1\n");
      const format = Buffer.from(
        "53434431000000020000000c0000bb800000000200000010",
        "hex",
      );
      const pcm = Buffer.concat([
        Buffer.from("534344310000000100000004", "hex"),
        Buffer.from([1, 2, 3, 4]),
      ]);
      socket.end(
        Buffer.concat([
          format,
          pcm,
          Buffer.from("534344310000000300000000", "hex"),
        ]),
      );
    });
  });
  await new Promise((resolve) => service.listen(12891, "127.0.0.1", resolve));
  try {
    const child = spawn(fixture, [], {
      env: { ...environment, STROOM_TEST_PCM: join(directory, "audio.pcm") },
      stdio: ["ignore", "pipe", "pipe"],
    });
    const completion = once(child, "close");
    const guard = setTimeout(() => child.kill("SIGKILL"), 5000);
    let log = "";
    for (const stream of [child.stdout, child.stderr])
      stream.on("data", (data) => {
        log += data;
        if (log.includes("PC playback: queued 0")) child.kill("SIGTERM");
      });
    try {
      const [code] = await completion;
      assert.equal(code, 143, log);
      return log;
    } finally {
      clearTimeout(guard);
      child.kill("SIGKILL");
    }
  } finally {
    for (const socket of sockets) socket.destroy();
    await new Promise((resolve) => service.close(resolve));
  }
}

try {
  await mkdir(home);
  let result = run(["--help"]);
  assert.equal(result.status, 0, result.stderr);
  await assert.rejects(access(configuration), { code: "ENOENT" });
  result = run();
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stdout.trim(), configuration);
  assert.deepEqual(JSON.parse(await readFile(configuration, "utf8")), {
    host: null,
    buffer_ms: 500,
  });
  assert.equal((await stat(configuration)).mode & 0o777, 0o600);
  const concurrent = join(directory, "concurrent");
  await Promise.all(
    Array.from({ length: 8 }, () =>
      execute(binary, ["--check-config"], {
        env: { ...environment, XDG_CONFIG_HOME: concurrent },
        timeout: 5000,
      }),
    ),
  );
  assert.deepEqual(
    JSON.parse(
      await readFile(join(concurrent, "stroom/listener.json"), "utf8"),
    ),
    { host: null, buffer_ms: 500 },
  );

  for (const text of [
    "",
    "{",
    "[]",
    "{} trailing",
    '{"buffer_ms":01}',
    '{"buffer_ms": 5,}',
    '{"buffer_ms":5 "host":null}',
    '{"buffer_ms" 5}',
    '{"buffer_ms":500,"buffer_ms":600}',
    '{"buffer_ms":500,"\\u0062uffer_ms":600}',
    '{"buffer_ms":2001}',
    '{"buffer_ms":-1}',
    '{"buffer_ms":1.5}',
    '{"buffer_ms":"500"}',
    '{"buffer_ms":null}',
    '{"host":""}',
    '{"host":"example.com"}',
    '{"host":true}',
    '{"port":12891}',
    '{"broadcast":null}',
    '{"host":"127.0.0.1\\u0000ignored"}',
    '{"log_statistics":false}',
    '{"unexpected":true}',
    '{"host":{}}',
    " ".repeat(4097),
  ]) {
    await writeFile(configuration, text);
    result = run();
    assert.equal(result.status, 1, text);
    assert(result.stderr.includes(configuration), result.stderr);
  }

  await writeFile(configuration, '{"host":"127.0.0.1","buffer_ms":1000}');
  let log = await listen();
  assert.match(log, /Buffering 1000 ms/);
  assert.match(log, /PC playback:/);
  for (const args of [
    ["--once"],
    ["--buffer-ms", "0"],
    ["--buffer-ms=1000"],
    ["--host", "127.0.0.1"],
    ["--port", "12891"],
    ["--broadcast", "127.0.0.1"],
  ]) {
    result = run(args);
    assert.equal(result.status, 1, args.join(" "));
    assert.match(result.stderr, /Unknown option/);
  }
  await writeFile(configuration, '{"host":"127.0.0.1","buffer_ms":0}');
  log = await listen();
  assert.match(log, /Buffering 0 ms/);

  await writeFile(join(directory, "other.json"), "{}");
  result = run(["--config", "other.json", "--check-config"]);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stdout.trim(), join(directory, "other.json"));
  result = run(["--config", "missing.json", "--check-config"]);
  assert.equal(result.status, 1);
  await assert.rejects(access(join(directory, "missing.json")), {
    code: "ENOENT",
  });
  result = run(["--check-config"], { XDG_CONFIG_HOME: "relative" });
  assert.equal(result.status, 0, result.stderr);
  assert.equal(
    result.stdout.trim(),
    join(home, ".config/stroom/listener.json"),
  );
  console.log(
    "Listener config creation, strict settings, file selection, native playback and XDG paths passed",
  );
} finally {
  await rm(directory, { recursive: true, force: true });
}
