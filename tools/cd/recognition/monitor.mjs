import { statfs } from "node:fs/promises";

export const MONITOR_INTERVAL_MS = 60000;
const WINDOW_MS = 300000;
const REPEAT_MS = 300000;
const MAX_HOSTS = 64;

/** Count actual queued work, excluding cache reads, idle results and retry timers. */
export function queueSnapshot(jobs, now = performance.now()) {
  const result = {
    recognition: { waiting: 0, running: 0, oldestWaitingMs: 0 },
    artwork: { waiting: 0, running: 0, oldestWaitingMs: 0 },
  };
  for (const job of jobs) {
    if (job.running) ++result[job.running].running;
    if (!job.pending || job.working || job.cachePending) continue;
    const queue = result[job.recognitionComplete ? "artwork" : "recognition"];
    ++queue.waiting;
    queue.oldestWaitingMs = Math.max(
      queue.oldestWaitingMs,
      Math.round(Math.max(0, now - job.queuedAt)),
    );
  }
  return result;
}

const counts = () => ({
  requests: 0,
  failures: 0,
  cancelled: 0,
  protocolErrors: 0,
  timeouts: 0,
  rateLimited: 0,
  notFound: 0,
  deferred: 0,
});

/** Local diagnostics only: bounded counters, periodic JSON logs and transitions. */
export class Monitor {
  #providers = new Map();
  #warnings = new Map();
  #storage = new Map();
  #storageFailures = 0;
  #outcomes = { matched: 0, noMatch: 0, failed: 0 };
  #slowRecognitionChecks = 0;
  #overQuotaSince = null;
  #lastEvictions = 0;
  #sampling = false;
  #observedErrors = new WeakSet();
  #lastHttpRejected = {};

  constructor({
    now = () => performance.now(),
    wallNow = () => Date.now(),
    filesystem = statfs,
    log = null,
  } = {}) {
    Object.assign(this, { now, wallNow, filesystem, log });
  }

  #emit(event) {
    this.log?.(event);
  }

  #condition(name, active, details = {}, severity = "warning") {
    const previous = this.#warnings.get(name);
    if (!active) {
      if (previous) {
        this.#warnings.delete(name);
        this.#emit({ event: "recovered", name });
      }
      return;
    }
    const now = this.now();
    if (
      !previous ||
      previous.severity !== severity ||
      now - previous.loggedAt >= REPEAT_MS
    ) {
      this.#warnings.set(name, { severity, loggedAt: now });
      this.#emit({ event: severity, name, ...details });
    }
  }

  #provider(host) {
    // Image URLs may introduce additional hosts; never retain unbounded labels.
    if (!this.#providers.has(host) && this.#providers.size >= MAX_HOSTS - 1)
      host = "other";
    let provider = this.#providers.get(host);
    if (!provider) {
      provider = {
        buckets: new Map(),
        inFlight: 0,
        lastHttpSuccessAt: null,
        cooldownUntil: 0,
      };
      this.#providers.set(host, provider);
    }
    return provider;
  }

  #bucket(provider) {
    const second = Math.floor(this.now() / 1000);
    for (const key of provider.buckets.keys())
      if (key <= second - WINDOW_MS / 1000) provider.buckets.delete(key);
    let bucket = provider.buckets.get(second);
    if (!bucket) {
      bucket = counts();
      provider.buckets.set(second, bucket);
    }
    return bucket;
  }

  providerStarted(host) {
    ++this.#provider(host).inFlight;
  }

  providerFinished(
    host,
    { failed = false, timeout = false, cancelled = false, status } = {},
  ) {
    const provider = this.#provider(host);
    provider.inFlight = Math.max(0, provider.inFlight - 1);
    const bucket = this.#bucket(provider);
    ++bucket.requests;
    if (failed) ++bucket.failures;
    else if (!cancelled)
      provider.lastHttpSuccessAt = new Date(this.wallNow()).toISOString();
    if (cancelled) ++bucket.cancelled;
    if (timeout) ++bucket.timeouts;
    if (status === 429) ++bucket.rateLimited;
    if (status === 404) ++bucket.notFound;
  }

  providerCooldown(host, deadline) {
    const provider = this.#provider(host);
    provider.cooldownUntil = Math.max(provider.cooldownUntil, deadline);
  }

  providerDeferred(host) {
    ++this.#bucket(this.#provider(host)).deferred;
  }

  observedError(error) {
    this.#observedErrors.add(error);
  }

  /** Record decoding/validation failures without counting transport errors twice. */
  providerError(host, error) {
    if (this.#observedErrors.has(error)) return;
    this.observedError(error);
    ++this.#bucket(this.#provider(host)).protocolErrors;
  }

  providers() {
    return Object.fromEntries(
      [...this.#providers].map(([host, provider]) => {
        this.#bucket(provider);
        const total = counts();
        for (const bucket of provider.buckets.values())
          for (const key of Object.keys(total)) total[key] += bucket[key];
        return [
          host,
          {
            ...total,
            inFlight: provider.inFlight,
            lastHttpSuccessAt: provider.lastHttpSuccessAt,
            cooldownUntil:
              provider.cooldownUntil > this.wallNow()
                ? new Date(provider.cooldownUntil).toISOString()
                : null,
          },
        ];
      }),
    );
  }

  lookup(outcome) {
    ++this.#outcomes[outcome];
  }

  storageFailure(operation, error) {
    ++this.#storageFailures;
    const details = { operation, code: error.code ?? error.name ?? "Error" };
    this.#storage.set(operation, details);
    this.#condition("storage-" + operation, true, details);
  }

  storageSuccess(operation) {
    this.#storage.delete(operation);
    this.#condition("storage-" + operation, false);
  }

  /** Never overlap filesystem samples or let a failed sample affect service work. */
  async sample({ queues, cache, directory, memoryCache, http }) {
    if (this.#sampling) return;
    this.#sampling = true;
    try {
      let freeBytes = null;
      if (directory) {
        try {
          const info = await this.filesystem(directory);
          freeBytes = Number(info.bavail) * Number(info.bsize);
          this.storageSuccess("filesystem");
        } catch (error) {
          this.storageFailure("filesystem", error);
        }
      }
      this.#slowRecognitionChecks =
        queues.recognition.oldestWaitingMs > 60000
          ? this.#slowRecognitionChecks + 1
          : 0;
      this.#condition(
        "recognition-queue",
        this.#slowRecognitionChecks >= 2,
        queues.recognition,
      );
      this.#condition(
        "artwork-queue",
        queues.artwork.oldestWaitingMs > 180000,
        queues.artwork,
      );
      const providers = this.providers();
      for (const [host, provider] of Object.entries(providers))
        this.#condition(
          "provider-" + host,
          provider.failures + provider.protocolErrors >= 5 &&
            (provider.failures + provider.protocolErrors) * 2 >=
              provider.requests - provider.cancelled,
          { host, ...provider },
        );
      if (
        directory &&
        (cache.bytes > cache.maxBytes ||
          cache.discs > cache.maxDiscs ||
          cache.files > cache.maxFiles)
      )
        this.#overQuotaSince ??= this.now();
      else this.#overQuotaSince = null;
      this.#condition(
        "cache-quota",
        this.#overQuotaSince !== null &&
          this.now() - this.#overQuotaSince >= 300000,
        {
          bytes: cache.bytes,
          maxBytes: cache.maxBytes,
          discs: cache.discs,
          maxDiscs: cache.maxDiscs,
          files: cache.files,
          maxFiles: cache.maxFiles,
        },
      );
      this.#condition("cache-disabled", !directory);
      if (freeBytes !== null)
        this.#condition(
          "disk-free",
          freeBytes < 1_000_000_000,
          { freeBytes },
          freeBytes < 250_000_000 ? "critical" : "warning",
        );
      for (const [operation, details] of this.#storage)
        this.#condition("storage-" + operation, true, details);
      this.#emit({
        event: "summary",
        ...queues,
        ...(memoryCache ? { memoryCache } : {}),
        ...(http
          ? {
              http: {
                ...http,
                rejectedSinceLastSummary: Object.fromEntries(
                  Object.entries(http.rejected).map(([reason, count]) => [
                    reason,
                    count - (this.#lastHttpRejected[reason] ?? 0),
                  ]),
                ),
              },
            }
          : {}),
        lookupsSinceLastSummary: { ...this.#outcomes },
        providerWindowSeconds: WINDOW_MS / 1000,
        providers,
        cache: {
          enabled: Boolean(directory),
          ...cache,
          evictionsSinceLastSummary: cache.evictions - this.#lastEvictions,
        },
        storage: {
          freeBytes,
          failuresSinceLastSummary: this.#storageFailures,
          unresolvedOperations: [...this.#storage.keys()],
        },
      });
      this.#outcomes = { matched: 0, noMatch: 0, failed: 0 };
      this.#storageFailures = 0;
      this.#lastEvictions = cache.evictions;
      if (http) this.#lastHttpRejected = { ...http.rejected };
    } finally {
      this.#sampling = false;
    }
  }
}

// CLI tools may accumulate diagnostics silently; the service attaches its logger.
export const monitor = new Monitor();
