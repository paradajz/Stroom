import assert from "node:assert/strict";
import { CoverBandwidth } from "../../../../../tools/cd/recognition/cover-bandwidth.mjs";

// Controlled monotonic time proves the shared allowance, without wall-clock thresholds.
let now = 0;
const limiter = new CoverBandwidth(1024, 1024, { now: () => now });
await limiter.wait(1024);
let first = false,
  second = false;
const one = new AbortController();
const two = new AbortController();
const a = limiter.wait(512, one.signal).then(() => {
  first = true;
});
const b = limiter.wait(512, two.signal).then(() => {
  second = true;
});
await Promise.resolve();
assert.equal(first, false);
assert.equal(second, false);
now = 500;
// Cancelling a later transfer rechecks refill without bypassing the earlier waiter.
const cancel = new AbortController();
const cancelled = limiter.wait(1, cancel.signal);
cancel.abort(Error("disconnected"));
await assert.rejects(cancelled, /disconnected/);
await a;
assert.equal(first, true);
assert.equal(second, false, "Two callers share one refill");
now = 1000;
const check = new AbortController();
const checked = limiter.wait(1, check.signal);
check.abort(Error("deadline"));
await assert.rejects(checked, /deadline/);
await b;
assert.equal(second, true);
const pending = limiter.wait(1, one.signal);
one.abort(Error("stopped"));
await assert.rejects(pending, /stopped/);
assert.throws(() => limiter.wait(1025), /burst/);
assert.throws(() => limiter.wait(1, one.signal), /stopped/);
now += 100000;
await limiter.wait(1024);
assert.equal(
  limiter.tokens,
  0,
  "Idle time cannot accumulate more than the burst",
);
console.log(
  "PASS: shared cover speed, bounded burst, FIFO progress, cancellation and idle refill.",
);
