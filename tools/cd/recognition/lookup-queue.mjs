import { SERVICE_LIMITS, HTTP_LIMITS } from "./config.mjs";

const MAX_OUTSTANDING = 2;
const BURST = 2;
const TOKEN_MS = 10000;
export const MAX_QUEUED_RECOGNITION = SERVICE_LIMITS.lookup.max_queued;

/** Bound new provider work per source IP and rotate queued work between callers. */
export class LookupQueue {
  #callers = new Map();
  #owners = new Map();
  #ready = [];
  #lastCaller;
  #queued = 0;

  constructor({
    now = Date.now,
    maxCallers = HTTP_LIMITS.trackedIps,
    maxQueued = MAX_QUEUED_RECOGNITION,
  } = {}) {
    this.now = now;
    this.maxCallers = maxCallers;
    this.maxQueued = maxQueued;
  }

  #refill(caller, now) {
    caller.tokens = Math.min(
      BURST,
      caller.tokens + Math.max(0, now - caller.at) / TOKEN_MS,
    );
    caller.at = now;
  }

  enqueue(job, address) {
    if (this.#owners.has(job)) return true;
    if (this.#queued >= this.maxQueued) return false;
    const now = this.now();
    let caller = this.#callers.get(address);
    if (!caller) {
      if (this.#callers.size >= this.maxCallers) {
        // Only forget callers whose work and admission cooldown have finished.
        for (const [key, entry] of this.#callers) {
          this.#refill(entry, now);
          if (!entry.outstanding && entry.tokens === BURST) {
            this.#callers.delete(key);
            break;
          }
        }
        if (this.#callers.size >= this.maxCallers) return false;
      }
      caller = { address, tokens: BURST, at: now, outstanding: 0, queue: [] };
      this.#callers.set(address, caller);
    }
    this.#refill(caller, now);
    if (
      caller.queue.length ||
      caller.outstanding >= MAX_OUTSTANDING ||
      caller.tokens < 1
    )
      return false;
    --caller.tokens;
    ++caller.outstanding;
    if (!caller.queue.length) this.#ready.push(address);
    caller.queue.push(job);
    this.#owners.set(job, caller);
    ++this.#queued;
    return true;
  }

  next() {
    if (!this.#ready.length) return null;
    // A caller that queued again while running yields to other ready callers.
    if (this.#ready.length > 1 && this.#ready[0] === this.#lastCaller)
      this.#ready.push(this.#ready.shift());
    const address = this.#ready.shift();
    const caller = this.#callers.get(address);
    const job = caller.queue.shift();
    --this.#queued;
    if (caller.queue.length) this.#ready.push(address);
    this.#lastCaller = address;
    return job;
  }

  /** Release a completed or cancelled job; admission tokens replenish with time. */
  finish(job) {
    const caller = this.#owners.get(job);
    if (!caller) return;
    this.#owners.delete(job);
    --caller.outstanding;
    const index = caller.queue.indexOf(job);
    if (index >= 0) {
      --this.#queued;
      caller.queue.splice(index, 1);
      if (!caller.queue.length)
        this.#ready = this.#ready.filter(
          (address) => address !== caller.address,
        );
    }
  }
}
