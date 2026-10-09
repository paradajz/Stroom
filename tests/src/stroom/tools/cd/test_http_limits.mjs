import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import {
  HttpLimits,
  HTTP_LIMITS,
} from "../../../../../tools/cd/recognition/http-limits.mjs";
class Socket extends EventEmitter {
  constructor(address) {
    super();
    this.remoteAddress = address;
    this.destroyed = false;
  }
  destroy() {
    if (!this.destroyed) {
      this.destroyed = true;
      this.emit("close");
    }
  }
}
let now = 0;
const create = () => new HttpLimits({ now: () => now });
const connect = (limits, address) => {
  const socket = new Socket(address);
  limits.connection(socket);
  return socket;
};
const attempt = (limits, address) => {
  const socket = connect(limits, address);
  const result = limits.request(socket);
  socket.destroy();
  return result;
};
let limits = create(),
  sockets = [];
for (let i = 0; i < 64; ++i)
  sockets.push(connect(limits, "ip-" + Math.floor(i / 8)));
assert.equal(limits.connections, 64);
assert.equal(connect(limits, "extra").destroyed, true);
assert.equal(limits.rejected.connectionsGlobal, 1);
for (const socket of sockets) socket.destroy();
assert.equal(limits.connections, 0);
limits = create();
sockets = [];
for (let i = 0; i < 8; ++i) sockets.push(connect(limits, "same"));
assert.equal(connect(limits, "same").destroyed, true);
assert.equal(limits.rejected.connectionsIp, 1);
const other = connect(limits, "other");
assert.equal(other.destroyed, false);
other.destroy();
for (const socket of sockets) socket.destroy();
limits = create();
for (let i = 0; i < 10; ++i) assert.equal(attempt(limits, "caller"), null);
assert.deepEqual(attempt(limits, "caller"), { status: 429, retryAfter: 1 });
now = 999;
assert.deepEqual(attempt(limits, "caller"), { status: 429, retryAfter: 1 });
now = 1000;
assert.equal(attempt(limits, "caller"), null);
limits = create();
for (let i = 0; i < 10; ++i) assert.equal(attempt(limits, "noisy"), null);
for (let i = 0; i < 50; ++i) assert.equal(attempt(limits, "noisy").status, 429);
for (let i = 0; i < 30; ++i)
  assert.equal(
    attempt(limits, "fair-" + i),
    null,
    "Rejected callers must not consume the shared request allowance",
  );
limits = create();
for (let i = 0; i < 40; ++i) assert.equal(attempt(limits, "ip-" + i), null);
assert.deepEqual(attempt(limits, "extra"), { status: 503, retryAfter: 1 });
now += 50;
assert.equal(attempt(limits, "extra"), null);
limits = create();
sockets = [];
const releases = [];
for (let i = 0; i < 8; ++i) {
  const socket = connect(limits, "cover-" + i);
  sockets.push(socket);
  assert.equal(limits.request(socket), null);
  releases.push(limits.cover(socket));
  assert.equal(typeof releases.at(-1), "function");
  if (i === 0) {
    const extra = connect(limits, "cover-0");
    assert.equal(limits.cover(extra), null);
    extra.destroy();
  }
}
const extra = connect(limits, "extra-cover");
assert.equal(limits.cover(extra), null);
extra.destroy();
assert.equal(limits.rejected.coversIp, 1);
assert.equal(limits.rejected.coversGlobal, 1);
for (const socket of sockets) socket.destroy();
assert.equal(
  limits.covers,
  8,
  "Closing sockets cannot release outstanding file operations",
);
now += 60000;
assert.equal(
  limits.snapshot().trackedIps,
  8,
  "Active covers protect peer accounting",
);
for (const release of releases) {
  release();
  release();
}
assert.equal(limits.covers, 0);
assert.equal(limits.snapshot().trackedIps, 0);
limits = create();
for (let i = 0; i < HTTP_LIMITS.trackedIps; ++i)
  attempt(limits, "history-" + i);
assert.equal(limits.peers.size, 1024);
assert.equal(connect(limits, "new-peer").destroyed, true);
assert.equal(limits.rejected.trackingFull, 1);
assert.equal(
  limits.peers.get("history-0").tokens,
  9,
  "Saturation cannot evict a caller and reset its allowance",
);
now += 6000;
const fresh = connect(limits, "new-peer");
assert.equal(fresh.destroyed, false);
fresh.destroy();
assert.ok(limits.peers.size <= HTTP_LIMITS.trackedIps);
limits = create();
const pipeline = connect(limits, "pipeline");
assert.equal(limits.request(pipeline), null);
assert.deepEqual(limits.request(pipeline), { status: 0 });
assert.equal(pipeline.destroyed, true);
assert.equal(limits.rejected.pipelined, 1);
console.log(
  "PASS: HTTP connection/transfer caps, fair request budgets across reconnects, refill boundaries, bounded protected caller history and pipelining rejection.",
);

limits = create();
for (let i = 0; i < 5; ++i) {
  const socket = connect(limits, "covers");
  assert.equal(limits.request(socket), null);
  assert.equal(limits.coverRequest(socket), 0);
  socket.destroy();
}
const overCover = connect(limits, "covers");
assert.equal(limits.request(overCover), null);
assert.equal(limits.coverRequest(overCover), 6);
overCover.destroy();
assert.equal(
  attempt(limits, "covers"),
  null,
  "Cover pacing must not consume the remaining metadata allowance",
);
now += 1000;
assert.equal(
  limits.snapshot().trackedIps,
  1,
  "Spent cover allowances protect history even after general tokens replenish",
);

limits = create();
for (let i = 0; i < 5; ++i) {
  const socket = connect(limits, "limited-cover");
  assert.equal(limits.request(socket, true), null);
  socket.destroy();
}
for (let i = 0; i < 10; ++i) {
  const socket = connect(limits, "limited-cover");
  assert.equal(limits.request(socket, true).status, 429);
  socket.destroy();
}
assert.equal(
  limits.global.tokens,
  35,
  "A caller over its cover allowance must not spend the shared HTTP allowance",
);
