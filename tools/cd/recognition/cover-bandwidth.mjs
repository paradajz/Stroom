import { once } from "node:events";

/** Shared cover-body pacing. Only admitted transfers can wait here (eight by default). */
export class CoverBandwidth {
  #pending = [];
  #timer;
  constructor(speed, burst, { now = () => performance.now() } = {}) {
    if (
      ![speed, burst].every((value) => Number.isSafeInteger(value) && value > 0)
    )
      throw Error("Cover speed and burst must be positive integers");
    this.speed = speed;
    this.burst = burst;
    this.tokens = burst;
    this.now = now;
    this.at = now();
  }

  #drain() {
    clearTimeout(this.#timer);
    this.#timer = undefined;
    const now = this.now();
    this.tokens = Math.min(
      this.burst,
      this.tokens + (Math.max(0, now - this.at) * this.speed) / 1000,
    );
    this.at = Math.max(this.at, now);
    while (this.#pending.length) {
      const entry = this.#pending[0];
      if (this.tokens < entry.size) {
        this.#timer = setTimeout(
          () => this.#drain(),
          Math.max(
            1,
            Math.ceil(((entry.size - this.tokens) * 1000) / this.speed),
          ),
        );
        this.#timer.unref();
        return;
      }
      this.#pending.shift();
      this.tokens -= entry.size;
      entry.signal?.removeEventListener("abort", entry.abort);
      entry.resolve();
    }
  }

  wait(size, signal) {
    signal?.throwIfAborted();
    if (!Number.isSafeInteger(size) || size <= 0 || size > this.burst)
      throw Error("Cover chunk exceeds its burst allowance");
    return new Promise((resolve, reject) => {
      const entry = { size, signal, resolve };
      entry.abort = () => {
        const index = this.#pending.indexOf(entry);
        if (index < 0) return;
        this.#pending.splice(index, 1);
        signal.removeEventListener("abort", entry.abort);
        reject(signal.reason);
        this.#drain();
      };
      signal?.addEventListener("abort", entry.abort, { once: true });
      this.#pending.push(entry);
      this.#drain();
    });
  }

  async send(response, bytes, signal) {
    const chunkSize = Math.min(16384, this.burst);
    for (let offset = 0; offset < bytes.length; offset += chunkSize) {
      const chunk = bytes.subarray(offset, offset + chunkSize);
      await this.wait(chunk.length, signal);
      signal.throwIfAborted();
      if (!response.write(chunk)) await once(response, "drain", { signal });
    }
    response.end();
  }
}
