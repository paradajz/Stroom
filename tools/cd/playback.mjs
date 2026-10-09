import { audio, cdStream as wire } from "../contracts/load.mjs";
import { performance } from "node:perf_hooks";

/** PC startup and recovery reserve policy; also used by the listener CLI. */
export const PC_BUFFER = Object.freeze({
  defaultMs: 500,
  minMs: 0,
  maxMs: 2000,
});

export function parseBufferMs(value = PC_BUFFER.defaultMs) {
  const milliseconds =
    typeof value === "string" && /^\d+$/.test(value) ? Number(value) : value;
  if (
    !Number.isInteger(milliseconds) ||
    milliseconds < PC_BUFFER.minMs ||
    milliseconds > PC_BUFFER.maxMs
  )
    throw Error(
      `PC buffer must be between ${PC_BUFFER.minMs} and ${PC_BUFFER.maxMs} milliseconds`,
    );
  return milliseconds;
}

/** Refill the PCM reserve at startup and when delivery outlasts queued audio. */
export async function* prebufferPcm(
  records,
  milliseconds,
  { now = () => performance.now(), onRebuffer } = {},
) {
  milliseconds = parseBufferMs(milliseconds);
  const bytesPerFrame = wire.CD_STREAM_CHANNELS * (wire.CD_STREAM_BITS / 8);
  const bytesPerSecond = audio.AUDIO_RATE * bytesPerFrame;
  const target =
    Math.ceil((audio.AUDIO_RATE * milliseconds) / 1000) * bytesPerFrame;
  let pending = [],
    bytes = 0,
    ready = target === 0;
  // Estimate the reserve already handed to FFplay from nominal PCM duration.
  // Withhold new samples after exhaustion; leave the player and stream open.
  let playbackUntil;
  for await (const record of records) {
    if (
      record.type === "pcm" &&
      target > 0 &&
      ready &&
      playbackUntil !== undefined &&
      now() > playbackUntil
    ) {
      ready = false;
      onRebuffer?.();
    }
    if (!ready && record.type === "pcm") {
      pending.push(record.data);
      bytes += record.data.length;
      if (bytes < target) continue;
      ready = true;
      playbackUntil = now() + (bytes / bytesPerSecond) * 1000;
      yield { type: "pcm", data: Buffer.concat(pending, bytes) };
      pending = [];
      bytes = 0;
    } else {
      // A clean short clip still plays. Cancellation/error discards its reserve.
      if (record.type === "end" && bytes) {
        yield { type: "pcm", data: Buffer.concat(pending, bytes) };
        pending = [];
        bytes = 0;
      }
      if (record.type === "pcm")
        playbackUntil =
          Math.max(now(), playbackUntil ?? 0) +
          (record.data.length / bytesPerSecond) * 1000;
      yield record;
    }
  }
}
