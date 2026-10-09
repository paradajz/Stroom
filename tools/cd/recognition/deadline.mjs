import { SERVICE_LIMITS } from "./config.mjs";

export const RECOGNITION_TIMEOUT_MS = SERVICE_LIMITS.lookup.recognition_timeout;
export const ARTWORK_TIMEOUT_MS = SERVICE_LIMITS.lookup.artwork_timeout;
const jobs = new WeakMap();

/** One monotonic budget, created only when a worker starts its attempt. */
export class JobDeadline {
  constructor(phase, milliseconds, { now = () => performance.now() } = {}) {
    this.controller = new AbortController();
    this.signal = this.controller.signal;
    this.now = now;
    this.expiresAt = now() + milliseconds;
    this.error = Object.assign(
      Error(`CD ${phase} exceeded its ${milliseconds / 1000}-second deadline`),
      {
        code: "CD_JOB_TIMEOUT",
        retryable: true,
      },
    );
    this.timer = setTimeout(
      () => this.controller.abort(this.error),
      milliseconds,
    );
    this.timer.unref();
    jobs.set(this.signal, this);
  }

  check(cause) {
    // Preserve cooldowns if later parsing or publication consumes the budget.
    if (Number.isSafeInteger(cause?.retryAt) && cause.retryAt > 0)
      this.error.retryAt = Math.max(this.error.retryAt ?? 0, cause.retryAt);
    if (!this.signal.aborted && this.now() >= this.expiresAt)
      this.controller.abort(this.error);
    throwAborted(this.signal, cause);
  }

  dispose() {
    clearTimeout(this.timer);
    jobs.delete(this.signal);
  }
}

function throwAborted(signal, cause) {
  if (!signal?.aborted) return;
  // Cancellation must retain an earlier provider's longer Retry-After deadline.
  if (Number.isSafeInteger(cause?.retryAt) && signal.reason instanceof Error)
    signal.reason.retryAt = Math.max(signal.reason.retryAt ?? 0, cause.retryAt);
  signal.throwIfAborted();
}

export function checkSignal(signal, cause) {
  const job = signal && jobs.get(signal);
  if (job) job.check(cause);
  else throwAborted(signal, cause);
}

/** Combine cancellation sources without requiring newer AbortSignal APIs. */
export function combineSignals(signals) {
  const controller = new AbortController();
  const subscriptions = [];
  for (const signal of signals.filter(Boolean)) {
    const abort = () => controller.abort(signal.reason);
    if (signal.aborted) abort();
    else {
      signal.addEventListener("abort", abort, { once: true });
      subscriptions.push([signal, abort]);
    }
  }
  return {
    signal: controller.signal,
    dispose: () => {
      for (const [signal, abort] of subscriptions)
        signal.removeEventListener("abort", abort);
    },
  };
}

/** Shared cooldown writes continue safely; the cancelled job stops awaiting them. */
export function waitWithSignal(promise, signal) {
  if (!signal) return promise;
  return new Promise((resolve, reject) => {
    const abort = () => {
      signal.removeEventListener("abort", abort);
      reject(signal.reason);
    };
    signal.addEventListener("abort", abort, { once: true });
    Promise.resolve(promise)
      .then(resolve, reject)
      .finally(() => signal.removeEventListener("abort", abort));
    if (signal.aborted) abort();
  });
}
