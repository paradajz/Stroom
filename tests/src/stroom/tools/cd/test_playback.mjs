import assert from "node:assert/strict";
import {
  audio,
  cdStream as wire,
} from "../../../../../tools/contracts/load.mjs";
import {
  PC_BUFFER,
  parseBufferMs,
  prebufferPcm,
} from "../../../../../tools/cd/playback.mjs";

const pcm = (value) => ({
  type: "pcm",
  data: Buffer.alloc(wire.CD_STREAM_MAX_PAYLOAD_BYTES, value),
});
const format = { type: "format" },
  end = { type: "end" };
async function collect(records, milliseconds, options) {
  const result = [];
  for await (const record of prebufferPcm(records, milliseconds, options))
    result.push(record);
  return result;
}
const short = [format, pcm(1), pcm(2), end];
const decodedShort = await collect(short, 500);
assert.deepEqual(
  decodedShort.map((r) => r.type),
  ["format", "pcm", "end"],
);
assert.deepEqual(
  decodedShort[1].data,
  Buffer.concat([short[1].data, short[2].data]),
);
assert.deepEqual(await collect(short, 0), short);
assert.deepEqual(await collect([format, end], 500), [format, end]);
assert.equal(parseBufferMs(), PC_BUFFER.defaultMs);
for (const value of [PC_BUFFER.minMs, PC_BUFFER.defaultMs, PC_BUFFER.maxMs]) {
  assert.equal(parseBufferMs(value), value);
  assert.equal(parseBufferMs(String(value)), value);
}
const invalidBuffer = `PC buffer must be between ${PC_BUFFER.minMs} and ${PC_BUFFER.maxMs} milliseconds`;
for (const value of [
  PC_BUFFER.minMs - 1,
  PC_BUFFER.maxMs + 1,
  1.5,
  NaN,
  Infinity,
  "",
  "nope",
  "0x10",
  "1.5",
  " 500",
  null,
  false,
]) {
  assert.throws(() => parseBufferMs(value), { message: invalidBuffer });
  await assert.rejects(collect(short, value), { message: invalidBuffer });
}

// Cancellation before the reserve is ready must never start stale playback.
const cancelled = [];
await assert.rejects(
  (async () => {
    async function* interrupted() {
      yield format;
      yield pcm(7);
      throw Error("CD playback interrupted");
    }
    for await (const record of prebufferPcm(interrupted(), 500))
      cancelled.push(record);
  })(),
  /interrupted/,
);
assert.deepEqual(cancelled, [format]);

// Virtual time models a 48 kHz player with delivery jitter, without wall-clock races.
async function simulate(milliseconds, gaps = []) {
  let now = 0,
    playbackEnds,
    underruns = 0,
    startedAt,
    rebufferings = 0;
  const expected = [],
    received = [];
  async function* jittered() {
    yield format;
    for (let i = 0; i < 300; ++i) {
      now += [10, 30, 0][i % 3];
      if (gaps.includes(i)) now += 900;
      const record = pcm(i);
      expected.push(record.data);
      yield record;
    }
    yield end;
  }
  for await (const record of prebufferPcm(jittered(), milliseconds, {
    now: () => now,
    onRebuffer: () => ++rebufferings,
  })) {
    if (record.type !== "pcm") continue;
    startedAt ??= now;
    if (playbackEnds !== undefined && now > playbackEnds + 0.1) ++underruns;
    playbackEnds =
      Math.max(now, playbackEnds ?? now) +
      (record.data.length / (audio.AUDIO_RATE * 4)) * 1000;
    received.push(record.data);
  }
  assert.deepEqual(Buffer.concat(received), Buffer.concat(expected));
  return { underruns, startedAt, rebufferings };
}
assert(
  (await simulate(0)).underruns > 0,
  "The fixture must reproduce unbuffered underruns",
);
const buffered = await simulate(500);
assert.equal(buffered.underruns, 0);
assert(buffered.startedAt >= 500);
assert.equal(buffered.rebufferings, 0);
const recovered = await simulate(500, [100, 200]);
assert.equal(recovered.rebufferings, 2);
assert.equal(
  recovered.underruns,
  2,
  "Each long outage causes one pause; later jitter stays protected",
);
const unbuffered = await simulate(0, [100, 200]);
assert.equal(
  unbuffered.rebufferings,
  0,
  "A zero reserve disables automatic rebuffering",
);
assert(unbuffered.underruns > recovered.underruns);

// A backlog arriving in a burst refills immediately without dropping samples.
let clock = 0,
  refills = 0;
const batch = Array.from({ length: 40 }, (_, i) => pcm(i));
async function* burst() {
  yield format;
  yield* batch;
  clock = 2000;
  yield* batch;
  yield end;
}
const burstOutput = await collect(burst(), 500, {
  now: () => clock,
  onRebuffer: () => ++refills,
});
assert.equal(refills, 1);
assert.deepEqual(
  Buffer.concat(burstOutput.filter((r) => r.type === "pcm").map((r) => r.data)),
  Buffer.concat([...batch, ...batch].map((r) => r.data)),
);

// END drains a short recovery reserve; cancellation discards that same reserve.
for (const cancelled of [false, true]) {
  clock = 0;
  const received = [];
  async function* ending() {
    yield format;
    yield* batch;
    clock = 2000;
    yield pcm(99);
    if (cancelled) throw Error("CD playback interrupted");
    yield end;
  }
  const consume = async () => {
    for await (const record of prebufferPcm(ending(), 500, {
      now: () => clock,
    }))
      received.push(record);
  };
  if (cancelled) await assert.rejects(consume(), /interrupted/);
  else await consume();
  assert.deepEqual(
    Buffer.concat(received.filter((r) => r.type === "pcm").map((r) => r.data)),
    Buffer.concat(
      [...batch, ...(cancelled ? [] : [pcm(99)])].map((r) => r.data),
    ),
  );
  assert.equal(received.at(-1).type === "end", !cancelled);
}
console.log(
  "PC buffering absorbs jitter, recovers after outages, preserves PCM and handles short clips and cancellation",
);
