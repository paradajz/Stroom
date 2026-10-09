#!/usr/bin/env node
import net from "node:net";
import { mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import { audio, cdStream as wire } from "../../contracts/load.mjs";

const frameBytes = (wire.CD_STREAM_CHANNELS * wire.CD_STREAM_BITS) / 8;
const bytesPerSecond = audio.AUDIO_RATE * frameBytes;
const magic = Buffer.from(wire.CD_STREAM_MAGIC);
const usage = `Usage: node tools/cd/listener/diagnose-pcm.mjs PS2_IP [--seconds 5]
Record received CD audio directly to a temporary WAV without opening PC audio.
Run while the fault is audible, leaving the PS2 and existing listener running.
Opens one additional PS2 audio subscription; captures at most 30 seconds.`;

/**
 * Capture bounded PCM from a fresh, validated TCP audio subscription.
 * @param {object} source Numeric host, port and positive capture seconds (at most 30).
 * @returns {Promise<Buffer>} PCM received before clean END or the capture deadline.
 */
export function capturePcm({ host, port = wire.CD_STREAM_PORT, seconds = 5 }) {
  if (!net.isIPv4(host) || !(seconds > 0 && seconds <= 30))
    return Promise.reject(Error("Invalid capture address or duration"));
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host, port });
    const chunks = [];
    let pending = Buffer.alloc(0),
      formatted = false,
      received = 0,
      done = false;
    let timer = setTimeout(
      () => finish(Error("No CD audio received within five seconds")),
      5000,
    );

    /**
     * Settle capture once and release its timer and socket.
     * @param {Error} error Optional validation or transport failure.
     */
    function finish(error) {
      if (done) return;
      done = true;
      clearTimeout(timer);
      socket.destroy();
      if (error) reject(error);
      else if (!received) reject(Error("Capture contains no CD audio"));
      else resolve(Buffer.concat(chunks, received));
    }

    socket.once("connect", () => socket.write(wire.CD_STREAM_REQUEST));
    socket.on("error", finish);
    socket.on("end", () =>
      finish(Error("CD stream ended without a clean END record")),
    );
    socket.on("data", (data) => {
      if (done) return;
      try {
        pending = Buffer.concat([pending, data]);
        while (pending.length >= wire.CD_STREAM_HEADER_BYTES) {
          const type = pending.readUInt32BE(4),
            size = pending.readUInt32BE(8);
          if (
            !pending.subarray(0, 4).equals(magic) ||
            size > wire.CD_STREAM_MAX_PAYLOAD_BYTES
          )
            throw Error("Invalid CD stream header or size");
          const length = wire.CD_STREAM_HEADER_BYTES + size;
          if (pending.length < length) break;
          const payload = pending.subarray(wire.CD_STREAM_HEADER_BYTES, length);
          pending = pending.subarray(length);
          if (
            !formatted &&
            type === wire.CD_STREAM_FORMAT &&
            size === wire.CD_STREAM_FORMAT_BYTES
          ) {
            if (
              payload.readUInt32BE(0) !== audio.AUDIO_RATE ||
              payload.readUInt32BE(4) !== wire.CD_STREAM_CHANNELS ||
              payload.readUInt32BE(8) !== wire.CD_STREAM_BITS
            )
              throw Error("Unsupported CD audio format");
            formatted = true;
          } else if (
            formatted &&
            type === wire.CD_STREAM_PCM &&
            size &&
            size % frameBytes === 0
          ) {
            if (!received) {
              clearTimeout(timer);
              timer = setTimeout(() => finish(), seconds * 1000);
            }
            received += size;
            if (received > bytesPerSecond * (seconds + 3))
              throw Error("Audio exceeds the capture size limit");
            chunks.push(Buffer.from(payload));
          } else if (formatted && type === wire.CD_STREAM_END && size === 0) {
            finish();
            return;
          } else throw Error("Unexpected CD stream record");
        }
      } catch (error) {
        finish(error);
      }
    });
  });
}

/**
 * Detect an exact short cycle sustained throughout the last three seconds.
 * @param {Buffer} pcm Interleaved PCM16 with complete stereo frames.
 * @returns {string} Description of silence, an exact cycle or no detected cycle.
 */
export function repetition(pcm) {
  const tail = pcm.subarray(Math.max(0, pcm.length - bytesPerSecond * 3));
  if (tail.every((byte) => byte === 0))
    return "Received audio ends in digital silence.";
  if (tail.length < bytesPerSecond * 2)
    return "Too little audio to check a sustained short loop.";
  const marker = tail.subarray(0, 64);
  for (
    let offset = tail.indexOf(marker, frameBytes);
    offset > 0 && offset <= bytesPerSecond / 2;
    offset = tail.indexOf(marker, offset + 1)
  ) {
    if (
      offset % frameBytes === 0 &&
      tail.subarray(offset).equals(tail.subarray(0, tail.length - offset))
    )
      return `Received PCM repeats exactly every ${((offset * 1000) / bytesPerSecond).toFixed(3)} ms across ${(tail.length / bytesPerSecond).toFixed(2)} seconds. The repetition is already in the data received from the PS2.`;
  }
  return "No exact loop of half a second or less found in the received PCM. Listen to the WAV to check the reported fault.";
}

/**
 * Wrap bounded PCM in a standard stereo PCM16 WAV file.
 * @param {Buffer} pcm Complete interleaved frames from capturePcm().
 * @returns {Buffer} RIFF/WAVE header and samples.
 */
export function wav(pcm) {
  const header = Buffer.alloc(44);
  header.write("RIFF");
  header.writeUInt32LE(36 + pcm.length, 4);
  header.write("WAVEfmt ", 8);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(wire.CD_STREAM_CHANNELS, 22);
  header.writeUInt32LE(audio.AUDIO_RATE, 24);
  header.writeUInt32LE(bytesPerSecond, 28);
  header.writeUInt16LE(frameBytes, 32);
  header.writeUInt16LE(wire.CD_STREAM_BITS, 34);
  header.write("data", 36);
  header.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([header, pcm]);
}

/**
 * Validate CLI options and save a direct received-audio recording.
 * @returns {Promise<void>} Resolves after printing the recording path and diagnosis.
 */
async function main() {
  const { values, positionals } = parseArgs({
    allowPositionals: true,
    options: {
      seconds: { type: "string", default: "5" },
      help: { type: "boolean", short: "h" },
    },
  });
  if (values.help) {
    console.log(usage);
    return;
  }
  const [host] = positionals;
  const seconds = Number(values.seconds);
  if (
    positionals.length !== 1 ||
    !net.isIPv4(host ?? "") ||
    !Number.isInteger(seconds) ||
    seconds < 1 ||
    seconds > 30
  )
    throw Error(usage);
  console.log(
    `Recording received audio for ${seconds} seconds; leave the PS2 playing...`,
  );
  const pcm = await capturePcm({ host, seconds });
  const path = join(
    await mkdtemp(join(tmpdir(), "stroom-pcm-")),
    "received.wav",
  );
  await writeFile(path, wav(pcm));
  console.log(
    `WAV: ${path}\nCaptured ${(pcm.length / bytesPerSecond).toFixed(2)} seconds of source audio.\n${repetition(pcm)}`,
  );
  console.log(
    "Exact repetition can also occur in music; this check identifies received bytes, not a drive error.",
  );
}

if (
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  try {
    await main();
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
