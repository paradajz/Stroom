import { lstat, readdir, unlink, utimes } from "node:fs/promises";
import { join } from "node:path";
import { DEFAULT_CONFIG, SERVICE_LIMITS, cacheTarget } from "./config.mjs";

export const CACHE_MAX_BYTES = DEFAULT_CONFIG.cache_max_size;
export const CACHE_TARGET_BYTES = cacheTarget(CACHE_MAX_BYTES);
export const CACHE_MAX_DISCS = SERVICE_LIMITS.cache.max_discs;
export const CACHE_TARGET_DISCS = SERVICE_LIMITS.cache.target_discs;
export const CACHE_MAX_FILES = SERVICE_LIMITS.cache.max_files;
export const CACHE_TARGET_FILES = SERVICE_LIMITS.cache.target_files;
const TOUCH_INTERVAL_MS = 60000;

/** Only files owned by the disc cache; provider state and unrelated files stay. */
export function cacheFileDisc(name) {
  return /^([A-Za-z0-9._]{27}-)(?:\.json(?:\.tmp)?|-[A-Za-z0-9._-]+\.jpg)$/u.exec(
    name,
  )?.[1];
}

/** Single-service disk accounting, with serialized maintenance and grouped LRU eviction. */
export class DiskCacheQuota {
  #entries = new Map();
  #holds = new Map();
  #bytes = 0;
  #files = 0;
  #evictions = 0;
  #trimming = false;
  #work = Promise.resolve();

  constructor(
    directory,
    {
      maxBytes = CACHE_MAX_BYTES,
      targetBytes = CACHE_TARGET_BYTES,
      maxDiscs = CACHE_MAX_DISCS,
      targetDiscs = CACHE_TARGET_DISCS,
      maxFiles = CACHE_MAX_FILES,
      targetFiles = CACHE_TARGET_FILES,
      isProtected = () => false,
      onEvict = () => {},
      onError = (error) => console.error("Cache quota: " + error.message),
      onSuccess = () => {},
    } = {},
  ) {
    if (!(targetBytes >= 0 && targetBytes < maxBytes))
      throw Error("Cache target must be smaller than its limit");
    for (const [limit, target] of [
      [maxDiscs, targetDiscs],
      [maxFiles, targetFiles],
    ])
      if (
        !Number.isSafeInteger(limit) ||
        !Number.isSafeInteger(target) ||
        target < 0 ||
        target >= limit
      )
        throw Error(
          "Cache count targets must be nonnegative integers smaller than their limits",
        );
    Object.assign(this, {
      directory,
      maxBytes,
      targetBytes,
      maxDiscs,
      targetDiscs,
      maxFiles,
      targetFiles,
      isProtected,
      onEvict,
      onError,
      onSuccess,
    });
  }

  #run(action, operation) {
    this.#work = this.#work
      .then(action)
      .catch((error) => this.onError(error, operation));
    return this.#work;
  }

  /** Reads wait for an eviction already in progress before opening cache files. */
  idle() {
    return this.#work;
  }

  snapshot() {
    return {
      bytes: this.#bytes,
      maxBytes: this.maxBytes,
      targetBytes: this.targetBytes,
      discs: this.#entries.size,
      maxDiscs: this.maxDiscs,
      targetDiscs: this.targetDiscs,
      files: this.#files,
      maxFiles: this.maxFiles,
      targetFiles: this.targetFiles,
      evictions: this.#evictions,
    };
  }

  async #record(name) {
    const id = cacheFileDisc(name);
    if (!id) return;
    let info;
    try {
      info = await lstat(join(this.directory, name));
    } catch (error) {
      if (error.code !== "ENOENT") throw error;
    }
    let entry = this.#entries.get(id);
    if (entry?.files.has(name)) {
      this.#bytes -= entry.files.get(name) ?? 0;
      --this.#files;
      entry.files.delete(name);
    }
    if (info?.isFile()) {
      if (!entry) {
        entry = { id, files: new Map(), used: 0, touched: 0 };
        this.#entries.set(id, entry);
      }
      entry.files.set(name, info.size);
      ++this.#files;
      entry.used = Math.max(entry.used, info.mtimeMs);
      this.#bytes += info.size;
    }
    if (entry && !entry.files.size) this.#entries.delete(id);
  }

  /** Scan once at startup; later writes update only the files that changed. */
  initialize() {
    return this.#run(async () => {
      if (!this.directory) return;
      for (const file of await readdir(this.directory, { withFileTypes: true }))
        if (file.isFile()) await this.#record(file.name);
      await this.#trim();
      this.onSuccess("scan");
    }, "scan");
  }

  record(...names) {
    return this.#run(async () => {
      if (!this.directory) return;
      for (const name of names) await this.#record(name);
      this.onSuccess("read");
      await this.#trim();
    }, "read");
  }

  /** Persist recent use without writing timestamps for every retry packet. */
  touch(id) {
    const entry = this.#entries.get(id);
    if (!entry) return this.idle();
    const now = Date.now();
    entry.used = now;
    if (now - entry.touched < TOUCH_INTERVAL_MS) return this.idle();
    entry.touched = now;
    return this.#run(async () => {
      // The entry may have been evicted before this operation was queued.
      if (this.#entries.get(id) !== entry) return;
      const name = entry.files.has(id + ".json")
        ? id + ".json"
        : entry.files.keys().next().value;
      try {
        const time = new Date(now);
        await utimes(join(this.directory, name), time, time);
        this.onSuccess("touch");
      } catch (error) {
        if (error.code !== "ENOENT") throw error;
      }
    }, "touch");
  }

  /** Protect a reader until its file bytes have been copied into memory. */
  hold(id) {
    if (!id) return () => {};
    this.#holds.set(id, (this.#holds.get(id) ?? 0) + 1);
    let released = false;
    return () => {
      if (released) return;
      released = true;
      const count = this.#holds.get(id) - 1;
      if (count) this.#holds.set(id, count);
      else this.#holds.delete(id);
      this.prune();
    };
  }

  prune() {
    if (!this.#trimming && !this.#aboveLimits()) return this.idle();
    return this.#run(() => this.#trim(), "delete");
  }

  #aboveLimits() {
    return (
      this.#bytes > this.maxBytes ||
      this.#entries.size > this.maxDiscs ||
      this.#files > this.maxFiles
    );
  }

  #atTargets() {
    return (
      this.#bytes <= this.targetBytes &&
      this.#entries.size <= this.targetDiscs &&
      this.#files <= this.targetFiles
    );
  }

  async #trim() {
    if (this.#aboveLimits()) this.#trimming = true;
    if (!this.#trimming) return;
    const oldest = [...this.#entries.values()].sort(
      (a, b) => a.used - b.used || a.id.localeCompare(b.id),
    );
    let deleted = false;
    let failed = false;
    for (const entry of oldest) {
      if (this.#atTargets()) break;
      if (this.#holds.has(entry.id) || this.isProtected(entry.id)) continue;
      // Invalidate idle in-memory metadata before yielding to file deletion.
      this.onEvict(entry.id);
      for (const [name, size] of entry.files) {
        try {
          await unlink(join(this.directory, name));
        } catch (error) {
          if (error.code !== "ENOENT") {
            failed = true;
            this.onError(error, "delete");
            continue;
          }
        }
        this.#bytes -= size;
        --this.#files;
        deleted = true;
        entry.files.delete(name);
      }
      if (!entry.files.size) {
        this.#entries.delete(entry.id);
        ++this.#evictions;
      }
    }
    if (deleted && !failed) this.onSuccess("delete");
    if (this.#atTargets()) this.#trimming = false;
  }
}
