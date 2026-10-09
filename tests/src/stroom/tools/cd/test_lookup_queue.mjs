import assert from "node:assert/strict";
import { LookupQueue } from "../../../../../tools/cd/recognition/lookup-queue.mjs";

let clock = 0;
const queue = new LookupQueue({ now: () => clock });
const a = [{}, {}, {}],
  b = [{}, {}],
  c = [{}, {}];
assert.ok(queue.enqueue(a[0], "A"));
assert.equal(queue.next(), a[0]);
assert.ok(queue.enqueue(a[1], "A"));
clock = 20000;
assert.equal(
  queue.enqueue(a[2], "A"),
  false,
  "Queued and running lookups both occupy a caller's slots",
);
for (const [caller, jobs] of [
  ["B", b],
  ["C", c],
]) {
  assert.ok(queue.enqueue(jobs[0], caller));
  assert.equal(
    queue.enqueue(jobs[1], caller),
    false,
    "An idle caller gets only one waiting slot",
  );
}
queue.finish(a[0]);
for (const expected of [b[0], c[0], a[1], b[1], c[1]]) {
  assert.equal(queue.next(), expected, "Ready callers must take turns");
  if (expected === b[0]) assert.ok(queue.enqueue(b[1], "B"));
  if (expected === c[0]) assert.ok(queue.enqueue(c[1], "C"));
  queue.finish(expected);
}
assert.equal(queue.next(), null);
assert.ok(queue.enqueue(a[2], "A"), "Completion releases outstanding capacity");
queue.finish(a[2]);
assert.equal(queue.next(), null, "Cancelling queued work removes its turn");

clock = 0;
const rate = new LookupQueue({ now: () => clock });
const job = {};
assert.ok(rate.enqueue(job, "A"));
assert.ok(
  rate.enqueue(job, "B"),
  "Joining an existing job must not consume another allowance",
);
const firstB = {},
  secondB = {};
assert.ok(rate.enqueue(firstB, "B"));
assert.equal(rate.enqueue(secondB, "B"), false);
rate.finish(firstB);
assert.ok(rate.enqueue(secondB, "B"));
rate.finish(secondB);
assert.equal(rate.enqueue({}, "B"), false);
rate.finish(job);
const second = {};
assert.ok(rate.enqueue(second, "A"));
rate.finish(second);
rate.finish(second);
assert.equal(
  rate.enqueue({}, "A"),
  false,
  "Finishing work must not refund admission tokens",
);
clock = 9999;
assert.equal(rate.enqueue({}, "A"), false);
clock = 10000;
assert.ok(rate.enqueue({}, "A"));
assert.equal(
  rate.enqueue({}, "A"),
  false,
  "Only one token replenishes every ten seconds",
);

clock = 0;
const bounded = new LookupQueue({ now: () => clock, maxCallers: 2 });
const held = {};
assert.ok(bounded.enqueue(held, "A"));
assert.equal(bounded.next(), held);
const completed = {};
assert.ok(bounded.enqueue(completed, "B"));
bounded.finish(completed);
assert.equal(
  bounded.enqueue({}, "C"),
  false,
  "Caller bookkeeping cannot discard an active cooldown",
);
clock = 10000;
assert.ok(bounded.enqueue({}, "C"));
assert.ok(bounded.enqueue({}, "A"));
assert.equal(
  bounded.enqueue({}, "A"),
  false,
  "Caller bookkeeping must retain outstanding work",
);
const capped = new LookupQueue();
const running = {},
  waiting = [{}, {}, {}],
  extra = {};
assert.ok(capped.enqueue(running, "running"));
assert.equal(capped.next(), running);
for (let i = 0; i < waiting.length; i++)
  assert.ok(capped.enqueue(waiting[i], "caller-" + i));
assert.equal(capped.enqueue(extra, "new-caller"), false);
assert.ok(capped.enqueue(waiting[0], "joining-caller"));
capped.finish(waiting[1]);
assert.ok(
  capped.enqueue(extra, "new-caller"),
  "A global rejection did not spend a caller token",
);
assert.equal(capped.enqueue({}, "another-caller"), false);
capped.next();
assert.ok(
  capped.enqueue({}, "another-caller"),
  "Starting work frees one waiting slot",
);

// Two busy keys leave a waiting slot for a third, whose admitted turn is retained.
const contested = new LookupQueue();
const activeA = {},
  nextA = {},
  activeB = {},
  nextB = {},
  legitimate = {};
assert.ok(contested.enqueue(activeA, "A"));
assert.equal(contested.next(), activeA);
assert.ok(contested.enqueue(nextA, "A"));
assert.ok(contested.enqueue(activeB, "B"));
assert.equal(contested.enqueue(nextB, "B"), false);
assert.ok(
  contested.enqueue(legitimate, "C"),
  "Two keys cannot fill all three waiting slots",
);
contested.finish(activeA);
assert.equal(contested.next(), activeB);
assert.ok(contested.enqueue(nextB, "B"));
contested.finish(activeB);
assert.equal(
  contested.next(),
  legitimate,
  "Refilling a running caller cannot jump ahead of an admitted caller",
);
console.log(
  "PASS: per-caller outstanding limits, timed admission, shared jobs, round-robin order, completion/cancellation and bounded caller bookkeeping.",
);
