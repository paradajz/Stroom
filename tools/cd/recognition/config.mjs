import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { dirname, isAbsolute, join, resolve } from "node:path";

export const CONFIG_TEMPLATE = new URL("./config.json", import.meta.url);
const defaults = JSON.parse(readFileSync(CONFIG_TEMPLATE, "utf8"));
const control = /[\x00-\x1f\x7f]/u;

// Protection rules stay fixed; the operator file only contains hosting choices.
export const SERVICE_LIMITS = Object.freeze({
  cache: Object.freeze({
    max_discs: 10000,
    target_discs: 9000,
    max_files: 20000,
    target_files: 18000,
    no_match_ttl: 86400000,
    failure_retry: 60000,
  }),
  memory: Object.freeze({ max_size: 64 * 1024 * 1024, max_results: 128 }),
  covers: Object.freeze({ burst: 1024 * 1024, timeout: 30000 }),
  lookup: Object.freeze({
    max_queued: 3,
    recognition_timeout: 60000,
    artwork_timeout: 120000,
    artwork_retry: 60000,
  }),
});

export const HTTP_LIMITS = Object.freeze({
  connections: 64,
  connectionsPerIp: 8,
  covers: 8,
  coversPerIp: 1,
  requestsPerSecond: 20,
  requestBurst: 40,
  requestsPerMinutePerIp: 60,
  requestBurstPerIp: 10,
  coverRequestsPerMinutePerIp: 10,
  coverRequestBurstPerIp: 5,
  trackedIps: 1024,
  headersMilliseconds: 5000,
});

export function cacheTarget(maxSize) {
  return maxSize - Math.ceil(maxSize / 10);
}

export function configPath() {
  const xdg = process.env.XDG_CONFIG_HOME;
  return join(
    xdg && isAbsolute(xdg) ? xdg : join(homedir(), ".config"),
    "stroom/config.json",
  );
}

export function cacheDirectory(
  platform = process.platform,
  home = homedir(),
  xdg = process.env.XDG_CACHE_HOME,
) {
  const base =
    xdg && isAbsolute(xdg)
      ? xdg
      : platform === "darwin"
        ? join(home, "Library", "Caches")
        : join(home, ".cache");
  return join(base, "stroom", "cd");
}

function fail(key, message) {
  throw Error(`Invalid setting ${key}: ${message}`);
}

function object(value, key) {
  if (!value || typeof value !== "object" || Array.isArray(value))
    fail(key, "expected an object");
}

function quantity(value, key, units) {
  if (typeof value !== "string") fail(key, "include a unit");
  const match = /^(\d+(?:\.\d+)?)\s*([A-Za-z/]+)$/u.exec(value.trim());
  const scale = match && units[match[2]];
  const number = scale && Number(match[1]) * scale;
  if (!Number.isSafeInteger(number) || number <= 0)
    fail(
      key,
      `expected a positive amount using ${Object.keys(units).join(", ")}`,
    );
  return number;
}

const sizes = {
  B: 1,
  KB: 1000,
  MB: 1e6,
  GB: 1e9,
  KiB: 1024,
  MiB: 1048576,
  GiB: 1073741824,
};
const speeds = Object.fromEntries(
  Object.entries(sizes).map(([unit, size]) => [unit + "/s", size]),
);

/** Merge partial operator settings with the shipped, human-readable defaults. */
export function parseConfig(
  input,
  { base = dirname(configPath()), requireContact = true } = {},
) {
  object(input, "configuration");
  for (const key of Object.keys(input))
    if (!Object.hasOwn(defaults, key)) fail(key, "unknown setting");
  const config = { ...defaults, ...input };
  if (
    typeof config.contact !== "string" ||
    control.test(config.contact) ||
    config.contact.length > 512 ||
    (requireContact && !config.contact.trim())
  )
    fail("contact", "set your real contact email or URL");
  if (typeof config.log_statistics !== "boolean")
    fail("log_statistics", "expected true or false");
  const directory = config.cache_directory;
  if (directory === null) config.cache_directory = cacheDirectory();
  else {
    if (
      typeof directory !== "string" ||
      !directory.trim() ||
      control.test(directory)
    )
      fail(
        "cache_directory",
        "expected a directory or null for the default cache",
      );
    const expanded =
      directory === "~"
        ? homedir()
        : directory.startsWith("~/")
          ? join(homedir(), directory.slice(2))
          : directory;
    config.cache_directory = resolve(base, expanded);
  }
  config.cache_max_size = quantity(
    config.cache_max_size,
    "cache_max_size",
    sizes,
  );
  config.cover_speed = quantity(config.cover_speed, "cover_speed", speeds);
  return config;
}

export const DEFAULT_CONFIG = parseConfig({}, { requireContact: false });

export function loadConfig(path = configPath(), { initialize = false } = {}) {
  path = resolve(path);
  try {
    let text;
    try {
      text = readFileSync(path, "utf8");
    } catch (error) {
      if (error.code !== "ENOENT" || !initialize) throw error;
      mkdirSync(dirname(path), { recursive: true });
      writeFileSync(path, readFileSync(CONFIG_TEMPLATE), {
        flag: "wx",
        mode: 0o600,
      });
      text = readFileSync(path, "utf8");
    }
    if (Buffer.byteLength(text) > 65536)
      throw Error("configuration exceeds 64 KiB");
    return {
      path,
      config: parseConfig(JSON.parse(text), { base: dirname(path) }),
    };
  } catch (error) {
    throw Error(`CD configuration ${path}: ${error.message}`);
  }
}
