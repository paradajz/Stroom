import { HTTP_LIMITS } from "./config.mjs";
export { HTTP_LIMITS } from "./config.mjs";

const rejectionCounts = () => ({
  connectionsGlobal: 0,
  connectionsIp: 0,
  trackingFull: 0,
  requestsGlobal: 0,
  requestsIp: 0,
  coverRequestsIp: 0,
  coversGlobal: 0,
  coversIp: 0,
  headersTimeout: 0,
  malformed: 0,
  unsupported: 0,
  pipelined: 0,
});
function take(bucket, capacity, rate, now) {
  bucket.tokens = Math.min(
    capacity,
    bucket.tokens + (Math.max(0, now - bucket.at) * rate) / 1000,
  );
  bucket.at = Math.max(bucket.at, now);
  if (bucket.tokens < 1 - 1e-9)
    return Math.max(1, Math.ceil((1 - bucket.tokens) / rate));
  bucket.tokens = Math.max(0, bucket.tokens - 1);
  return 0;
}

/** Bounded accounting by the IPv4 address of the TCP peer. */
export class HttpLimits {
  constructor({ now = () => performance.now(), limits = HTTP_LIMITS } = {}) {
    this.now = now;
    this.limits = limits;
    this.peers = new Map();
    this.sockets = new WeakMap();
    this.connections = 0;
    this.covers = 0;
    this.global = { tokens: this.limits.requestBurst, at: now() };
    this.rejected = rejectionCounts();
  }

  prune() {
    const now = this.now();
    for (const [address, peer] of this.peers) {
      const replenished =
        peer.tokens +
        (Math.max(0, now - peer.at) * this.limits.requestsPerMinutePerIp) /
          60000;
      if (
        !peer.connections &&
        !peer.covers &&
        replenished >= this.limits.requestBurstPerIp - 1e-9 &&
        peer.coverBucket.tokens +
          (Math.max(0, now - peer.coverBucket.at) *
            this.limits.coverRequestsPerMinutePerIp) /
            60000 >=
          this.limits.coverRequestBurstPerIp - 1e-9
      )
        this.peers.delete(address);
    }
  }

  peer(address) {
    let peer = this.peers.get(address);
    if (!peer) {
      if (this.peers.size >= this.limits.trackedIps) this.prune();
      if (this.peers.size >= this.limits.trackedIps) return null;
      peer = {
        tokens: this.limits.requestBurstPerIp,
        at: this.now(),
        connections: 0,
        covers: 0,
        coverBucket: {
          tokens: this.limits.coverRequestBurstPerIp,
          at: this.now(),
        },
      };
      this.peers.set(address, peer);
    }
    return peer;
  }

  connection(socket) {
    const reject = (reason) => {
      ++this.rejected[reason];
      socket.destroy();
      return false;
    };
    if (this.connections >= this.limits.connections)
      return reject("connectionsGlobal");
    const peer = this.peer(socket.remoteAddress);
    if (!peer) return reject("trackingFull");
    if (peer.connections >= this.limits.connectionsPerIp)
      return reject("connectionsIp");
    ++peer.connections;
    ++this.connections;
    const state = {
      peer,
      requested: false,
      timer: null,
      acceptedAt: performance.now(),
    };
    state.timer = setTimeout(() => {
      ++this.rejected.headersTimeout;
      socket.destroy();
    }, this.limits.headersMilliseconds);
    state.timer.unref();
    this.sockets.set(socket, state);
    socket.once("close", () => {
      clearTimeout(state.timer);
      this.sockets.delete(socket);
      --peer.connections;
      --this.connections;
    });
    return true;
  }

  request(socket, cover = false) {
    const state = this.sockets.get(socket);
    if (!state || socket.destroyed) return { status: 0 };
    if (state.requested) {
      ++this.rejected.pipelined;
      socket.destroy();
      return { status: 0 };
    }
    state.requested = true;
    clearTimeout(state.timer);
    const now = this.now();
    const ipRetry = take(
      state.peer,
      this.limits.requestBurstPerIp,
      this.limits.requestsPerMinutePerIp / 60,
      now,
    );
    if (ipRetry) {
      ++this.rejected.requestsIp;
      return { status: 429, retryAfter: ipRetry };
    }
    if (cover) {
      const retryAfter = this.coverRequest(socket);
      if (retryAfter) return { status: 429, retryAfter };
    }
    // A caller already over its own limit cannot spend everyone's allowance.
    const globalRetry = take(
      this.global,
      this.limits.requestBurst,
      this.limits.requestsPerSecond,
      now,
    );
    if (globalRetry) {
      ++this.rejected.requestsGlobal;
      return { status: 503, retryAfter: globalRetry };
    }
    return null;
  }

  deadline(socket) {
    return (
      (this.sockets.get(socket)?.acceptedAt ?? performance.now()) +
      this.limits.headersMilliseconds
    );
  }

  coverRequest(socket) {
    const peer = this.sockets.get(socket)?.peer;
    if (!peer) return 1;
    const retry = take(
      peer.coverBucket,
      this.limits.coverRequestBurstPerIp,
      this.limits.coverRequestsPerMinutePerIp / 60,
      this.now(),
    );
    if (retry) ++this.rejected.coverRequestsIp;
    return retry;
  }

  cover(socket) {
    const peer = this.sockets.get(socket)?.peer;
    if (!peer || socket.destroyed) return null;
    if (this.covers >= this.limits.covers) {
      ++this.rejected.coversGlobal;
      return null;
    }
    if (peer.covers >= this.limits.coversPerIp) {
      ++this.rejected.coversIp;
      return null;
    }
    ++this.covers;
    ++peer.covers;
    let released = false;
    return () => {
      if (released) return;
      released = true;
      --this.covers;
      --peer.covers;
    };
  }

  snapshot() {
    this.prune();
    return {
      connections: this.connections,
      activeCovers: this.covers,
      trackedIps: this.peers.size,
      limits: this.limits,
      rejected: { ...this.rejected },
    };
  }
}
