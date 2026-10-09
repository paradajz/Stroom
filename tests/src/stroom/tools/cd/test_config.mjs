import assert from "node:assert/strict";
import {
  mkdtempSync,
  readFileSync,
  writeFileSync,
  existsSync,
  rmSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import {
  parseConfig,
  loadConfig,
  HTTP_LIMITS,
  SERVICE_LIMITS,
  cacheTarget,
  configPath,
} from "../../../../../tools/cd/recognition/config.mjs";

const contact = "test@example.test";
const config = parseConfig({ contact });
assert.deepEqual(
  Object.keys(config).sort(),
  [
    "contact",
    "cache_directory",
    "cache_max_size",
    "cover_speed",
    "log_statistics",
  ].sort(),
);
assert.equal(config.cache_max_size, 1_000_000_000);
assert.equal(cacheTarget(config.cache_max_size), 900_000_000);
assert.equal(cacheTarget(1), 0);
assert.equal(config.cover_speed, 10 * 1024 * 1024);
assert.equal(SERVICE_LIMITS.covers.burst, 1024 * 1024);
assert.equal(SERVICE_LIMITS.lookup.recognition_timeout, 60000);
assert.equal(HTTP_LIMITS.coversPerIp, 1);
const custom = parseConfig(
  {
    contact,
    cache_directory: "./cache",
    cache_max_size: "1.5 GB",
    cover_speed: "500 KiB/s",
  },
  { base: "/config" },
);
assert.equal(custom.cache_directory, "/config/cache");
assert.equal(custom.cache_max_size, 1500000000);
assert.equal(cacheTarget(custom.cache_max_size), 1350000000);
assert.equal(custom.cover_speed, 512000);
assert.throws(() => parseConfig({}), /contact/);
for (const [settings, pattern] of [
  [{ contact: "bad\ncontact" }, /contact/],
  [{ contact: null }, /contact/],
  [{ max_downloads_per_ip: 2 }, /unknown setting/],
  [{ burst: "1 MiB" }, /unknown setting/],
  [{ log_statistics: "false" }, /log_statistics/],
  [{ monitor: true }, /unknown setting/],
  [{ typo: true }, /unknown setting/],
  [{ http: {} }, /unknown setting/],
  [{ cache: {} }, /unknown setting/],
  [{ lookup: {} }, /unknown setting/],
  [{ covers: {} }, /unknown setting/],
  [{ memory: {} }, /unknown setting/],
  [{ logging: {} }, /unknown setting/],
  [{ cache_directory: 1 }, /cache_directory/],
  [{ cache_directory: "bad\npath" }, /cache_directory/],
  [{ cache_max_size: 1000000000 }, /include a unit/],
  [{ cache_max_size: "1 XB" }, /positive amount/],
  [{ cache_max_size: "0 MiB" }, /positive amount/],
  [{ cover_speed: "10 MiB" }, /positive amount/],
])
  assert.throws(() => parseConfig({ contact, ...settings }), pattern);

const directory = mkdtempSync(join(tmpdir(), "stroom-config-test-"));
try {
  const path = join(directory, "settings/config.json");
  assert.throws(
    () => loadConfig(path, { initialize: true }),
    /set your real contact/,
  );
  const template = JSON.parse(readFileSync(path, "utf8"));
  assert.equal(template.cover_speed, "10 MiB/s");
  assert.equal(Object.keys(template).length, 5);
  writeFileSync(path, JSON.stringify({ contact, cache_directory: "../cache" }));
  assert.equal(
    loadConfig(path).config.cache_directory,
    join(directory, "cache"),
  );
  assert.equal(loadConfig(path).path, path);
  assert.ok(
    !readFileSync(path, "utf8").includes("cache_max_size"),
    "Loading never rewrites operator settings",
  );
  assert.throws(() => loadConfig(join(directory, "missing.json")), /ENOENT/);
  writeFileSync(path, "{");
  assert.throws(() => loadConfig(path), /CD configuration .*config.json/);
  const service = fileURLToPath(
    new URL("../../../../../tools/cd/recognition/service.mjs", import.meta.url),
  );
  const result = spawnSync(process.execPath, [service], {
    encoding: "utf8",
    env: { ...process.env, STROOM_CD_CONFIG: "", XDG_CONFIG_HOME: directory },
  });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /contact/);
  assert.ok(existsSync(join(directory, "stroom/config.json")));
  assert.ok(!result.stdout.includes("listening"));
} finally {
  rmSync(directory, { recursive: true, force: true });
}
assert.ok(configPath().endsWith("/stroom/config.json"));
console.log(
  "PASS: readable config units, partial defaults, strict validation, fixed per-IP cover limit, relative paths and first-run setup.",
);
